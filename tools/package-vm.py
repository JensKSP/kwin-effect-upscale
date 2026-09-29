#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install a package in a standard installation of a system and check it there.

    package-vm.py SYSTEM create [--replace]   a fresh machine, logged in to Plasma
    package-vm.py SYSTEM start | stop | status
    package-vm.py SYSTEM guest COMMAND...     a command in the guest
    package-vm.py SYSTEM check PACKAGE        install, check, remove; report

A machine per system and architecture, each the system's own cloud image with
the Plasma desktop its installer offers, logged in by SDDM into Plasma's Wayland
session: what a person has before installing this package. Its screen is 4K,
since the effect acts from 1920 x 1080 up by default, and Mesa's llvmpipe draws
it; no GPU of the host is given to it. The check installs the package with the
system's package manager, logs in again, and checks the effect, the settings
module, the X11 proxy, a game on Wayland and one through X11, and removal (see
tools/package_check.py). Everything of a machine lives under build/<name>. How a
machine is made is tools/virtual_machine.py's.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

import virtual_machine as vm

# One screen of 3840 x 2160 on the display device, no other.
SCREEN = (
    "-display",
    "none",
    "-vga",
    "none",
    "-device",
    "VGA,edid=on,xres=3840,yres=2160,vgamem_mb=64",
)
SYSTEMS = {
    "debian-13-amd64": vm.Machine(
        name="package-debian-13-amd64",
        template="containers/vm-host/plasma-debian.in",
        ready="pgrep -u tester -x kwin_wayland >/dev/null",
        display=SCREEN,
        # The first boot installs the whole desktop.
        boot_seconds=5400,
    ),
    "kubuntu-26.04-amd64": vm.Machine(
        name="package-kubuntu-26.04-amd64",
        template="containers/vm-host/plasma-kubuntu.in",
        cloud="https://cloud-images.ubuntu.com/resolute/current/",
        base="resolute-server-cloudimg-amd64.img",
        sums="SHA256SUMS",
        algorithm="sha256",
        ready="pgrep -u tester -x kwin_wayland >/dev/null",
        display=SCREEN,
        boot_seconds=7200,
    ),
}


def check(machine: vm.Machine, package: Path) -> int:
    """Copy the package into the machine's directory, and run the check in the guest."""
    copied = machine.directory / package.name
    shutil.copyfile(package, copied)
    report = f"{machine.shared}/report.json"
    command = vm.ssh(
        machine,
        *("sudo", "python3", "-B", "/src/tools/package_check.py"),
        *("--package", f"{machine.shared}/{package.name}", "--report", report),
    )
    return subprocess.run(command, cwd=vm.ROOT, check=False).returncode


def main(argv: list[str] | None = None) -> int:
    """Carry out one command on one system's machine."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("system", choices=sorted(SYSTEMS))
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("create").add_argument("--replace", action="store_true")
    commands.add_parser("start")
    commands.add_parser("stop")
    commands.add_parser("status")
    # Everything after the command's name is the guest's, options included.
    commands.add_parser("guest").add_argument("words", nargs=argparse.REMAINDER)
    commands.add_parser("check").add_argument("package", type=Path)
    arguments = parser.parse_args(argv)
    machine = SYSTEMS[arguments.system]
    if arguments.command == "create":
        vm.create(machine, arguments.replace)
    elif arguments.command == "start":
        vm.start(machine)
    elif arguments.command == "stop":
        vm.stop(machine)
    elif arguments.command == "status":
        print(vm.state(machine))
    elif arguments.command == "check":
        return check(machine, arguments.package)
    else:
        guest = vm.ssh(machine, *arguments.words)
        return subprocess.run(guest, cwd=vm.ROOT, check=False).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
