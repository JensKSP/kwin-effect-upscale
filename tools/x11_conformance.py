#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run XTS against rootful Xwayland without a window manager changing its windows.

KWin hosts the outer surface and supplies the production proxy's connection
policy. The startup connection claims WM_S0 to let Xwayland accept clients,
but selects no events and never manages a window. XTS keeps its verification
configuration, including XT_DEBUG_OVERRIDE_REDIRECT=No. Managed-window and
rendering behavior are covered separately by the KWin integration tests.
"""

from __future__ import annotations

import argparse
import contextlib
import errno
import os
import select
import socket
import struct
import subprocess
from pathlib import Path


def receive(connection: socket.socket, count: int) -> bytes:
    """Read a complete startup packet or fail on a closed connection."""
    data = bytearray()
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            message = "Xwayland closed its startup connection"
            raise RuntimeError(message)
        data.extend(chunk)
    return bytes(data)


def initialize_manager(connection: socket.socket) -> None:
    """Claim the startup selection without subscribing to window events."""
    connection.settimeout(10)
    connection.sendall(b"l\0" + struct.pack("<HHHHH", 11, 0, 0, 0, 0))
    header = receive(connection, 8)
    if header[0] != 1:
        message = "Xwayland rejected its startup connection"
        raise RuntimeError(message)
    setup = header + receive(connection, struct.unpack_from("<H", header, 6)[0] * 4)
    offset = 40 + ((struct.unpack_from("<H", setup, 24)[0] + 3) & ~3) + setup[29] * 8
    root = struct.unpack_from("<I", setup, offset)[0]
    connection.sendall(struct.pack("<BBHHH", 16, 0, 4, 5, 0) + b"WM_S0\0\0\0")
    reply = receive(connection, 32)
    if reply[0] != 1:
        message = "Xwayland did not return the startup selection atom"
        raise RuntimeError(message)
    atom = struct.unpack_from("<I", reply, 8)[0]
    connection.sendall(struct.pack("<BBHIII", 22, 0, 4, root, atom, 0))


def configure_xts(root: Path, environment: dict[str, str]) -> None:
    """Generate upstream's execution configuration against the test display."""
    tree = Path(environment["TET_ROOT"])
    with (
        (tree / "xts5" / "tetexec.cfg.in").open() as source,
        Path(environment["TET_CONFIG"]).open("w") as target,
    ):
        subprocess.run(
            ["perl", "-p", str(tree / "xts5" / "bin" / "xts-config")],
            stdin=source,
            stdout=target,
            env=environment,
            check=True,
            timeout=30,
        )
    # Retain the display and root event mask with the verdict for inspection.
    for name, command in (
        ("nested-display.txt", ["xdpyinfo"]),
        ("root-events.txt", ["xwininfo", "-root", "-events"]),
    ):
        with (root / name).open("w") as output:
            subprocess.run(command, env=environment, stdout=output, check=True, timeout=15)


def listener(stack: contextlib.ExitStack) -> tuple[str, socket.socket]:
    """Reserve a free test display without replacing another server's socket."""
    connection = stack.enter_context(socket.socket(socket.AF_UNIX))
    for number in range(90, 190):
        # X11's standard socket directory; bind fails if the path is occupied.
        path = Path(f"/tmp/.X11-unix/X{number}")  # noqa: S108  # nosec B108
        try:
            connection.bind(str(path))
        except OSError as error:
            # A live Unix socket is reported as EADDRINUSE, not EEXIST.
            if error.errno == errno.EADDRINUSE:
                continue
            raise
        stack.callback(path.unlink, missing_ok=True)
        connection.listen(128)
        return f":{number}", connection
    message = "No free XTS display socket"
    raise RuntimeError(message)


def run(root: Path, build: Path, arm: str, size: str, command: list[str]) -> int:
    """Run one complete suite with the requested production connection policy."""
    environment = dict(os.environ)
    environment.pop("WAYLAND_SOCKET", None)
    with contextlib.ExitStack() as stack:
        display, clients = listener(stack)
        manager, manager_child = socket.socketpair()
        stack.enter_context(manager)
        stack.enter_context(manager_child)
        read_ready, write_ready = os.pipe()
        ready = stack.enter_context(os.fdopen(read_ready, "rb", buffering=0))
        writer = stack.enter_context(os.fdopen(write_ready, "wb", buffering=0))
        arguments = [
            display,
            "-geometry",
            size,
            "-fullscreen",
            "-ac",
            "-nolisten",
            "tcp",
            "-noreset",
            "-listenfd",
            str(clients.fileno()),
            "-wm",
            str(manager_child.fileno()),
            "-displayfd",
            str(write_ready),
        ]
        descriptors = [clients.fileno(), manager_child.fileno(), write_ready]
        program = "/usr/bin/Xwayland"
        if arm != "bare":
            program = str(build / "bin" / "upscale_x11proxy_conformance")
            wayland = stack.enter_context(socket.socket(socket.AF_UNIX))
            wayland.connect(
                str(Path(environment["XDG_RUNTIME_DIR"]) / environment["WAYLAND_DISPLAY"])
            )
            environment["WAYLAND_SOCKET"] = str(wayland.fileno())
            descriptors.append(wayland.fileno())
        log = stack.enter_context((root / "nested.log").open("w"))
        server = subprocess.Popen(
            [program, *arguments], pass_fds=descriptors, env=environment, stdout=log, stderr=log
        )
        try:
            writer.close()
            manager_child.close()
            initialize_manager(manager)
            if not select.select([ready], [], [], 30)[0] or not ready.read(64).strip():
                message = "XTS Xwayland did not become ready"
                raise RuntimeError(message)
            environment.pop("WAYLAND_SOCKET", None)
            environment.update(
                DISPLAY=display,
                TET_ROOT="/opt/xts",
                TET_CONFIG=str(root / "config" / "tetexec.cfg"),
            )
            configure_xts(root, environment)
            return subprocess.run(command, env=environment, check=False).returncode
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


def main() -> int:
    """Start the internal session command invoked by check-conformance.py."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--arm", choices=("bare", "present", "scaling"), required=True)
    parser.add_argument("--size", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("missing suite command")
    return run(arguments.root, arguments.build, arguments.arm, arguments.size, command)


if __name__ == "__main__":
    raise SystemExit(main())
