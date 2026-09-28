# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an isolated KWin virtual session and propagate its test client's result."""

import json
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
    # The scale the session's outputs run at. A real desktop rarely runs a 4K
    # screen at one: the machine these tests describe runs it at three, which
    # puts every logical size a third of the pixels it is made of. Input and
    # presentation both cross that boundary, so the suite has to be able to
    # stand on either side of it.
    scale = next(
        (
            int(argument.removeprefix("--scale="))
            for argument in sys.argv[2:]
            if argument.startswith("--scale=")
        ),
        1,
    )
    # The mode the session's outputs run in, named once. The X11 tests place
    # windows by pixel and need a screen large enough to ask for a smaller one
    # inside it; the Wayland tests need no room at all.
    mode = (3840, 2160) if x11 else (128, 128)
    # How many outputs the session has. Two is what the X11 placement cases
    # need; a connection answered before its first window needs one, because
    # the screen an answer names is a screen and not an arrangement of them.
    # A Wayland session has one unless it asks, as its cases about an output
    # going away do.
    outputs = next(
        (
            int(argument.removeprefix("--outputs="))
            for argument in sys.argv[2:]
            if argument.startswith("--outputs=")
        ),
        2 if x11 else 1,
    )
    with tempfile.TemporaryDirectory(prefix="integration-", dir=build) as directory:
        runtime = Path(directory)
        config = runtime / "config"
        config.mkdir()
        # upscaleEnabled=false keeps an effect installed on this machine from
        # loading itself beside the module this test drives, so that what the
        # session runs is the copy built here and nothing else.
        (config / "kwinrc").write_text(
            "[Plugins]\nupscaleEnabled=false\n"
            "[Effect-upscale]\nEnabled=true\nSharpening=false\nMinimumPixels=0\n"
            "[Compositing]\nGLCore=true\n"
        )
        if scale != 1:
            # KWin identifies a virtual output by its connector name alone; it
            # has no EDID to hash. The mode has to match the one the backend is
            # started with, or the setup is discarded as inapplicable.
            # Named apart from the count: the command below still needs that.
            recorded = [
                {
                    "connectorName": f"Virtual-{index}",
                    "scale": scale,
                    "mode": {"width": mode[0], "height": mode[1], "refreshRate": 60000},
                    "transform": "Normal",
                }
                for index in range(outputs)
            ]
            # The outputs entry is only the database of what has been seen.
            # Without a setup naming them, KWin generates a configuration of
            # its own and the recorded scale is never applied.
            placement = [
                {
                    "enabled": True,
                    "outputIndex": index,
                    "priority": index,
                    "position": {"x": index * mode[0] // scale, "y": 0},
                }
                for index in range(outputs)
            ]
            (config / "kwinoutputconfig.json").write_text(
                json.dumps(
                    [
                        {"name": "outputs", "data": recorded},
                        {"name": "setups", "data": [{"lidClosed": False, "outputs": placement}]},
                    ]
                )
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
            # The tests place the pointer in KWin's logical pixels while they
            # place X11 windows in the device pixels X11 still counts in, so
            # they have to know which of the two this session separates.
            UPSCALE_TEST_OUTPUT_SCALE=str(scale),
            UPSCALE_TEST_OUTPUT_COUNT=str(outputs),
        )
        environment.pop("QT_QPA_PLATFORM", None)
        environment["QT_PLUGIN_PATH"] = str(build / "bin")
        command = [
            str(compositor),
            "--virtual",
            "--width",
            str(mode[0]),
            "--height",
            str(mode[1]),
            "--output-count",
            str(outputs),
            "--no-lockscreen",
            "--no-global-shortcuts",
            "--no-kactivities",
            "--exit-with-session",
            # Anything left over selects which test functions run, which is how
            # a session with an unusual setup covers a few of them rather than
            # paying for the whole suite a second time.
            shlex.join(
                [
                    str(binary),
                    *(
                        argument
                        for argument in sys.argv[2:]
                        if not argument.startswith(("--x11", "--scale=", "--outputs="))
                    ),
                ]
            ),
        ]
        if x11:
            command[1:1] = ["--xwayland"]
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
