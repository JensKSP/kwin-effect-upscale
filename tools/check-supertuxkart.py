#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
r"""SuperTuxKart in all six ways it presents itself, each enlarged by the effect.

The handbook's hard requirement: native Wayland and Xwayland, each with OpenGL
fullscreen, Vulkan borderless and Vulkan exclusive fullscreen. Each cell gets a
compositor of its own (KWin's virtual backend at 3840 x 2160), the effect from
--build at Quality, this check's own game list where the settings page's
import puts one, and SuperTuxKart with a fresh configuration of its own that
names the renderer and, for Vulkan, borderless or exclusive. A cell passes when
the effect enlarges the game's window from 2560 x 1440 to 3840 x 2160, and a
picture of the screen, taken with the game stopped on one frame, shows that
frame where KWin's own plain stretch of the same buffer shows it, in the same
colours and with more detail than that stretch has.

Run it in the conformance machine, in the game test image, which has the game
and Mesa's llvmpipe and lavapipe; never on a real GPU:

    tools/conformance-vm.py test --image localhost/upscale-game-tests:trixie \
        python3 -B tools/check-supertuxkart.py --build build/conformance-vm/effect
"""

from __future__ import annotations

import argparse
import json
import os
import runpy
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
session_environment = runpy.run_path(str(HERE / "check-presentations.py"))["session_environment"]
GAME_LIST = HERE.parent / "autotests/data/game-tests/kwinupscalerc"
SCREEN = (3840, 2160)
# Quality is two thirds of each side.
SUPPLIED = "2560x1440"
# How far apart two pictures of one frame may look, on 0 to 255, for them to
# show the same thing in the same place and colours. Measured 2026-09-29: 0.03
# to 0.15 between the effect's picture and KWin's plain stretch in every cell.
LIKENESS = 6.0
# How much more detail the effect's picture has to have than KWin's plain
# stretch of the same buffer. Measured 2026-09-29: 25 to 31 % more, without
# RCAS, in every cell.
DETAIL = 1.1


@dataclass(frozen=True)
class Cell:
    """One way SuperTuxKart presents itself."""

    name: str
    # SDL's video driver: native Wayland, or X11 through Xwayland.
    videodriver: str
    renderer: str
    # For Vulkan: SDL's desktop fullscreen, which is borderless, or exclusive.
    desktop: bool = True


CELLS = (
    Cell("wayland-opengl-fullscreen", "wayland", "gl"),
    Cell("wayland-vulkan-borderless", "wayland", "vulkan"),
    Cell("wayland-vulkan-exclusive", "wayland", "vulkan", desktop=False),
    Cell("xwayland-opengl-fullscreen", "x11", "gl"),
    Cell("xwayland-vulkan-borderless", "x11", "vulkan"),
    Cell("xwayland-vulkan-exclusive", "x11", "vulkan", desktop=False),
)


def game_configuration(cell: Cell) -> str:
    """SuperTuxKart's own settings: fullscreen at the screen's size, and the renderer."""
    return (
        '<?xml version="1.0"?>\n<stkconfig version="8" >\n'
        f'    <Video real_width="{SCREEN[0]}" real_height="{SCREEN[1]}" fullscreen="true"\n'
        f'        render_driver="{cell.renderer}"'
        f' vulkan_fullscreen_desktop="{"true" if cell.desktop else "false"}" />\n'
        "</stkconfig>\n"
    )


def prepare(directory: Path, cell: Cell) -> None:
    """Write the effect's settings, the game list and the game's own settings."""
    config = directory / "config"
    (config / "supertuxkart/config-0.10").mkdir(parents=True)
    # Quality is stored as 2. Only the listed game is acted on, and the
    # display stays off, so that the pictures show the game alone.
    (config / "kwinrc").write_text(
        "[Plugins]\nupscaleEnabled=true\n"
        "[Effect-upscale]\nEnabled=true\nResolution=2\nMinimumPixels=0\nSharpening=false\n"
        "Osd=false\nUnlistedApplications=false\n"
    )
    shutil.copyfile(GAME_LIST, config / "kwinupscalerc")
    (config / "supertuxkart/config-0.10/config.xml").write_text(game_configuration(cell))


# Said of a cell whose session left no result: it did not run, which is
# neither a pass nor a finding about the effect.
NOT_RUN = "not run: the session ended without a result"


def judge(result: dict[str, object]) -> list[str]:
    """Say what a cell's result falls short of; nothing when it passes."""
    if not result:
        return [NOT_RUN]
    if not result.get("enlarged"):
        return [f"the effect never enlarged the game: {result.get('waited')}"]
    if "failure" in result:
        return [f"a picture could not be taken: {result['failure']}"]
    problems = []
    during = result.get("during")
    reading = during if isinstance(during, dict) else {}
    if reading.get("supplied") != SUPPLIED:
        problems.append(f"supplied {reading.get('supplied')}, not {SUPPLIED}")
    if reading.get("destination") != f"{SCREEN[0]}x{SCREEN[1]}":
        problems.append(f"drawn to {reading.get('destination')}, not the whole screen")
    if reading.get("scaling") != "1":
        problems.append("the effect had stopped enlarging when the screen was captured")
    sizes = result.get("sizes")
    if not isinstance(sizes, dict) or sizes.get("screen") != f"{SCREEN[0]}x{SCREEN[1]}":
        problems.append(f"screen captured at {sizes}")
    likeness = result.get("likeness")
    likeness = likeness if isinstance(likeness, dict) else {}
    plain = likeness.get("plain")
    if not isinstance(plain, float) or plain > LIKENESS:
        problems.append(f"the screen does not show what KWin's plain stretch shows ({plain})")
    sharpness = result.get("sharpness")
    sharpness = sharpness if isinstance(sharpness, dict) else {}
    ours, theirs = sharpness.get("screen"), sharpness.get("plain")
    if not isinstance(ours, float) or not isinstance(theirs, float) or ours < theirs * DETAIL:
        problems.append(f"no more detail than KWin's plain stretch ({ours} against {theirs})")
    return problems


def run_cell(
    cell: Cell, root: Path, build: Path, track: str, seconds: float
) -> tuple[list[str], dict[str, object]]:
    """Run one cell in a session of its own, and say what it fell short of and what it found."""
    directory = root / cell.name
    directory.mkdir()
    runtime = Path(tempfile.mkdtemp(prefix="stk-"))
    runtime.chmod(0o700)
    environment = session_environment(directory, build, runtime)
    environment["KWIN_SCREENSHOT_NO_PERMISSION_CHECKS"] = "1"
    # X11 goes through the session proxy, as Plasma routes it by default: the
    # proxy is the build's Xwayland, found first on the path KWin searches.
    environment["PATH"] = f"{build / 'bin'}{os.pathsep}{environment.get('PATH', os.defpath)}"
    environment["UPSCALE_X11_SESSION_ROUTED"] = "1"
    prepare(directory, cell)
    game = shutil.which("supertuxkart") or "/usr/games/supertuxkart"
    environment["UPSCALE_CASE"] = json.dumps(
        {
            "directory": str(directory),
            "program": game,
            "arguments": ["-R", f"--track={track}", "--numkarts=1"],
            "environment": {"SDL_VIDEODRIVER": cell.videodriver},
            "seconds": seconds,
            "settle": 5.0,
            "supplied": SUPPLIED,
        }
    )
    probe = shlex.join([sys.executable, "-B", str(HERE / "supertuxkart_probe.py")])
    command = [
        *("dbus-run-session", "--", "kwin_wayland", "--virtual", "--xwayland"),
        *("--output-count", "1", "--width", str(SCREEN[0]), "--height", str(SCREEN[1])),
        *("--no-lockscreen", "--no-global-shortcuts", "--no-kactivities"),
        *("--exit-with-session", probe),
    ]
    with (directory / "session.log").open("w") as log:
        try:
            subprocess.run(
                command,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=seconds + 240,
                check=False,
            )
        except subprocess.TimeoutExpired:
            log.write("\nthe session did not end within its own time\n")
        finally:
            shutil.rmtree(runtime, ignore_errors=True)
    found = directory / "result.json"
    result = json.loads(found.read_text()) if found.exists() else {}
    return judge(result), result


def main(argv: list[str] | None = None) -> int:
    """Run the cells asked for, report each, and fail when any did not pass."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--build", type=Path, required=True, help="the build whose effect runs")
    parser.add_argument("--track", default="lighthouse")
    parser.add_argument("--seconds", type=float, default=180.0, help="how long a game may take")
    parser.add_argument("--cell", action="append", choices=[cell.name for cell in CELLS])
    arguments = parser.parse_args(argv)
    chosen = [cell for cell in CELLS if not arguments.cell or cell.name in arguments.cell]
    root = Path(tempfile.mkdtemp(prefix="supertuxkart-", dir=arguments.build))
    outcomes = {
        cell.name: run_cell(cell, root, arguments.build, arguments.track, arguments.seconds)
        for cell in chosen
    }
    report = [
        {"cell": name, "problems": problems, "result": result}
        for name, (problems, result) in outcomes.items()
    ]
    (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for name, (problems, result) in outcomes.items():
        status = "pass" if not problems else "NOT RUN" if problems == [NOT_RUN] else "FAIL"
        remarks = [
            *problems,
            *([f"KWin stalled: {result['stalled']}"] if "stalled" in result else []),
        ]
        print(f"{name:28} {status:8} {'; '.join(remarks)}")
    print(f"\nsessions, pictures and report under {root}")
    return 1 if any(problems for problems, _ in outcomes.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
