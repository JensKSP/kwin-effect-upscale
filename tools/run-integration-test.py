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
    # Test the effect the package installed, not a copy built beside the test.
    # The package stage passes this; a check job builds its own driver module
    # and points Qt at it instead.
    installed = "--installed" in sys.argv[2:]
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
        # Not named after the flag above: this is where the compositor lives,
        # and the flag says which effect it is to load.
        system_compositor = shutil.which("kwin_wayland")
        if not system_compositor:
            print("kwin_wayland is required for integration tests")
            return 1
        shutil.copyfile(system_compositor, compositor)
        compositor.chmod(0o700)
        environment = dict(os.environ)
        environment.update(
            XDG_RUNTIME_DIR=str(runtime),
            XDG_CONFIG_HOME=str(config),
            XDG_CACHE_HOME=str(runtime / "cache"),
            KWIN_COMPOSE="Q",
            LIBGL_ALWAYS_SOFTWARE="1",
            LC_ALL="C.UTF-8",
            QT_LOGGING_TO_CONSOLE="1",
            QT_FORCE_STDERR_LOGGING="1",
        )
        environment.pop("QT_QPA_PLATFORM", None)
        if installed:
            # Qt's own plugin path finds the installed effect. Setting
            # QT_PLUGIN_PATH here would shadow it with a build tree, which is
            # exactly what this run is not testing.
            environment.pop("QT_PLUGIN_PATH", None)
        else:
            environment["QT_PLUGIN_PATH"] = str(build / "bin")
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
        # How long the whole session may take, which is not how long the work
        # in it takes: every wait inside these tests is a round trip through
        # KWin, Xwayland and a client, and how fast those are is a property of
        # the machine. Measured on the nightly's arm64 package job,
        # 2026-09-20: it presents 4.5 frames a second, one frame every 3.1
        # seconds, and the run that takes 55 s here needed more than 160
        # there. At 90 s the session was killed mid-test and the case that
        # was still waiting got the blame, which is how this looked like a
        # plugin defect for most of a night.
        #
        # Instrumentation also observes allocations in the distribution KWin
        # process, so its startup and shutdown need more time again.
        timeout = 900 if preload else 600
        return subprocess.call(
            ["dbus-run-session", "--", *command], env=environment, timeout=timeout
        )


if __name__ == "__main__":
    raise SystemExit(main())
