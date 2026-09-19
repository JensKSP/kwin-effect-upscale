# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an isolated KWin virtual session and propagate its test client's result."""

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def isolate_xwayland(runtime: Path, environment: dict[str, str]) -> None:
    """Keep the compositor's sanitizer preload out of distribution X helpers."""
    installed = shutil.which("Xwayland")
    if not installed:
        message = "Xwayland is required for X11 integration tests"
        raise FileNotFoundError(message)
    # KWin finds Xwayland through PATH. Only this child drops the preload;
    # KWin, the effect and the instrumented test client retain their checks.
    wrapper = runtime / "Xwayland"
    wrapper.write_text(f'#!/bin/sh\nunset LD_PRELOAD\nexec {shlex.quote(installed)} "$@"\n')
    wrapper.chmod(0o700)
    environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", os.defpath)


def main() -> int:
    """Keep the bus, configuration, socket and compositor private to this test."""
    binary = Path(sys.argv[1]).resolve()
    build = binary.parent.parent
    x11 = "--x11" in sys.argv[2:]
    with tempfile.TemporaryDirectory(prefix="integration-", dir=build) as directory:
        runtime = Path(directory)
        config = runtime / "config"
        config.mkdir()
        (config / "kwinrc").write_text(
            "[Plugins]\nupscaleEnabled=false\n"
            "[Effect-upscale]\nEnabled=true\nSharpening=false\nMinimumPixels=0\n"
            "[Compositing]\nGLCore=true\n"
        )
        # Distribution KWin may carry file capabilities, which make the loader
        # ignore LD_PRELOAD. A private executable copy permits instrumentation
        # without changing the system binary or attaching to a real session.
        compositor = runtime / "kwin_wayland"
        installed = shutil.which("kwin_wayland")
        if not installed:
            print("kwin_wayland is required for integration tests")
            return 1
        shutil.copyfile(installed, compositor)
        compositor.chmod(0o700)
        environment = dict(os.environ)
        environment.update(
            XDG_RUNTIME_DIR=str(runtime),
            XDG_CONFIG_HOME=str(config),
            XDG_CACHE_HOME=str(runtime / "cache"),
            QT_PLUGIN_PATH=str(build / "bin"),
            KWIN_COMPOSE="Q",
            LIBGL_ALWAYS_SOFTWARE="1",
            LC_ALL="C.UTF-8",
            QT_LOGGING_TO_CONSOLE="1",
            QT_FORCE_STDERR_LOGGING="1",
        )
        environment.pop("QT_QPA_PLATFORM", None)
        command = [
            str(compositor),
            "--virtual",
            "--width",
            "3840" if x11 else "128",
            "--height",
            "2160" if x11 else "128",
            "--no-lockscreen",
            "--no-global-shortcuts",
            "--no-kactivities",
            "--exit-with-session",
            shlex.join([str(binary)]),
        ]
        if x11:
            command[1:1] = ["--xwayland", "--output-count", "2"]
        preload = environment.pop("UPSCALE_SANITIZER_RUNTIME", "")
        if preload:
            if x11:
                isolate_xwayland(runtime, environment)
            command = ["env", f"LD_PRELOAD={preload}", *command]
        # Instrumentation also observes allocations in the distribution KWin
        # process; startup and shutdown need more time than an ordinary run.
        timeout = 270 if preload else 90
        return subprocess.call(
            ["dbus-run-session", "--", *command], env=environment, timeout=timeout
        )


if __name__ == "__main__":
    raise SystemExit(main())
