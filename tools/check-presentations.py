#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what the effect decides about a real game in each way it can present.

The window tests beside this one are synthetic: a client written to make the
window a case needs. That is what let a borderless window go unhandled on
2026-09-25 while a test called "borderless" passed, because the test began at
the output's size and had a cooperative client resize, which is nothing a game
does. A game decides its own window, so only a game can say what the effect
meets in the field.

Each run starts a compositor of its own with the virtual backend, so nothing
here touches the session it is started from. The games are read from the
machine they run on; they are not in the check containers, and installing them
there would measure a different build of them than the one a person plays.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Mapping

sys.path.insert(0, str(Path(__file__).resolve().parent))


@dataclass(frozen=True)
class Screen:
    """The screen a run gives its games, and how long one game gets on it."""

    size: tuple[int, int]
    scale: int
    seconds: float


@dataclass(frozen=True)
class Case:
    """One game in one way of presenting itself, and what that should mean."""

    game: str
    presentation: str
    program: str
    arguments: tuple[str, ...] = ()
    environment: Mapping[str, str] = field(default_factory=dict)
    # The cell the effect is expected to place the window in, and whether it is
    # expected to act on it. A windowed window is never acted on: no method
    # obtains a smaller buffer from one, so the effect refuses it.
    expected: str = ""
    acted: bool = True
    # What the effect calls this game's window, so that a reading about some
    # other window is not read as an answer about this one.
    window: str = ""


def session_environment(root: Path, build: Path, runtime: Path) -> dict[str, str]:
    """Give the session its own configuration, cache and runtime.

    The runtime directory is not kept beside the logs. A Unix socket path may
    not exceed 108 bytes, and a path inside the build directory spends most of
    that before the compositor adds its own name to it, so the sockets live
    under the session's own runtime directory, which is short by design.
    """
    environment = dict(os.environ)
    for name in (
        "DISPLAY",
        "WAYLAND_DISPLAY",
        "WAYLAND_SOCKET",
        "XAUTHORITY",
        "QT_QPA_PLATFORM",
        "DBUS_SESSION_BUS_ADDRESS",
        "SESSION_MANAGER",
    ):
        environment.pop(name, None)
    environment.update(
        XDG_RUNTIME_DIR=str(runtime),
        XDG_CONFIG_HOME=str(root / "config"),
        XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"),
        QT_PLUGIN_PATH=str(build / "bin"),
        LIBGL_ALWAYS_SOFTWARE="1",
        LC_ALL="C.UTF-8",
        QT_LOGGING_TO_CONSOLE="1",
        # The session log is the only account of a case that did not present as
        # expected, and without this it holds none of the effect's reasoning.
        QT_LOGGING_RULES="kwin_effect_upscale.debug=true",
    )
    return environment


def write_configuration(root: Path, size: tuple[int, int], scale: int) -> None:
    """Switch the effect on below the screen's size, at the screen's scale."""
    config = root / "config"
    config.mkdir()
    # Resolution is stored as a number, Performance being 4, while the method
    # keys beside it are names; written as a name, it is not read at all and
    # the default, Quality, applies instead. UnlistedApplications is what the
    # settings page calls All games.
    (config / "kwinrc").write_text(
        "[Plugins]\nupscaleEnabled=true\n"
        "[Effect-upscale]\nEnabled=true\nResolution=4\n"
        "MinimumPixels=0\nSharpening=false\nOsd=false\nUnlistedApplications=true\n"
    )
    if scale != 1:
        outputs = [
            {
                "connectorName": "Virtual-0",
                "scale": scale,
                "transform": "Normal",
                "mode": {"width": size[0], "height": size[1], "refreshRate": 60000},
            }
        ]
        (config / "kwinoutputconfig.json").write_text(
            json.dumps(
                [
                    {"name": "outputs", "data": outputs},
                    {
                        "name": "setups",
                        "data": [
                            {
                                "lidClosed": False,
                                "outputs": [
                                    {
                                        "enabled": True,
                                        "outputIndex": 0,
                                        "priority": 0,
                                        "position": {"x": 0, "y": 0},
                                    }
                                ],
                            }
                        ],
                    },
                ]
            )
        )


def declare_game(root: Path, program: str) -> str:
    """Make the case's program a game to All games, as installing it would.

    All games acts only for a program it recognizes as a game, and a game run
    from where it was built or unpacked has no desktop entry that says so. One
    in the Game category, in the session's own data, starts this program.
    Answers why it could not, or nothing.
    """
    executable = Path(program).resolve()
    # Quoted as the effect reads an Exec line, which knows double quotes and
    # no escapes inside them.
    if '"' in str(executable):
        return f"{executable} has a double quote, which an Exec line cannot name"
    applications = root / "data" / "applications"
    applications.mkdir(parents=True)
    (applications / "upscale-case.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Upscale case\nCategories=Game;\n"
        f'Exec="{executable}"\n'
    )
    return ""


def run_case(case: Case, root: Path, build: Path, screen: Screen) -> dict[str, object]:
    """Start a compositor, run one game in it, and read what the effect said."""
    # The game's name carries the protocol, which is not a directory of its own.
    cell = root / f"{case.game}-{case.presentation}".replace("/", "-")
    cell.mkdir()
    runtime = Path(
        tempfile.mkdtemp(prefix="up-", dir=os.environ.get("XDG_RUNTIME_DIR", tempfile.gettempdir()))
    )
    runtime.chmod(0o700)
    environment = session_environment(cell, build, runtime)
    write_configuration(cell, screen.size, screen.scale)
    program = shutil.which(case.program)
    if not program:
        return {
            "game": case.game,
            "presentation": case.presentation,
            "outcome": "absent",
            "detail": f"{case.program} is not installed",
        }
    if problem := declare_game(cell, program):
        return {
            "game": case.game,
            "presentation": case.presentation,
            "outcome": "absent",
            "detail": problem,
        }
    inner = Path(__file__).resolve().parent / "presentation_probe.py"
    result = cell / "result.json"
    environment["UPSCALE_CASE"] = json.dumps(
        {
            "program": program,
            "arguments": list(case.arguments),
            "environment": dict(case.environment),
            "seconds": screen.seconds,
            "result": str(result),
            "log": str(cell / "game.log"),
            "window": case.window,
        }
    )
    compositor = shutil.which("kwin_wayland")
    if not compositor:
        return {
            "game": case.game,
            "presentation": case.presentation,
            "outcome": "absent",
            "detail": "kwin_wayland is not installed",
        }
    command = [
        "dbus-run-session",
        "--",
        compositor,
        "--virtual",
        "--xwayland",
        "--output-count",
        "1",
        "--width",
        str(screen.size[0]),
        "--height",
        str(screen.size[1]),
        "--no-lockscreen",
        "--no-global-shortcuts",
        "--no-kactivities",
        "--exit-with-session",
        shlex.join([sys.executable, "-B", str(inner)]),
    ]
    with (cell / "session.log").open("w") as log:
        try:
            subprocess.run(
                command,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=screen.seconds + 180,
                check=False,
            )
        except subprocess.TimeoutExpired:
            log.write("\nthe session did not end within its own time\n")
        finally:
            shutil.rmtree(runtime, ignore_errors=True)
    found = json.loads(result.read_text()) if result.exists() else {}
    presentation = str(found.get("presentation", ""))
    acted = bool(found.get("selected", False))
    supplied = str(found.get("supplied", ""))
    wanted = case.expected
    outcome = "pass" if presentation.endswith(wanted) and acted == case.acted else "fail"
    if not presentation:
        outcome = "unseen"
    return {
        "game": case.game,
        "presentation": case.presentation,
        "outcome": outcome,
        "classified": presentation or "-",
        "acted": acted,
        "expected": wanted,
        "wanted_acted": case.acted,
        "supplied": supplied or "-",
        "root": str(cell),
    }


def report(rows: list[dict[str, object]]) -> int:
    """One line per case, and a failure if any case did not run or disagreed.

    Every case here was asked for by name, so one whose game or compositor is
    not installed is a failure too, not a case to pass over.
    """
    width = max((len(f"{row['game']} {row['presentation']}") for row in rows), default=0)
    for row in rows:
        name = f"{row['game']} {row['presentation']}".ljust(width)
        if row["outcome"] == "absent":
            print(f"{name}  absent   {row['detail']}")
            continue
        print(
            f"{name}  {str(row['outcome']).ljust(7)} classified {row['classified']}"
            f" acted {row['acted']} (wanted {row['expected']}, acted {row['wanted_acted']})"
            f" supplied {row['supplied']}"
        )
    failed = [row for row in rows if row["outcome"] in ("fail", "unseen", "absent")]
    if failed:
        print(f"\n{len(failed)} of {len(rows)} cases did not run or did not present as expected")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    """Run the cases asked for and report what each one presented as."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build/native", help="the build whose effect is loaded")
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--scale", type=int, default=1, help="the screen's scale")
    parser.add_argument("--seconds", type=float, default=60.0, help="how long one game gets")
    parser.add_argument("--name", default="application", help="label for this case")
    parser.add_argument("--window", required=True, help="window identity in the effect's metrics")
    parser.add_argument(
        "--presentation", required=True, choices=["fullscreen", "borderless", "windowed"]
    )
    parser.add_argument(
        "--acted",
        action=argparse.BooleanOptionalAction,
        help="whether the effect should act on the window; by default it should unless windowed",
    )
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- program [arguments]")
    arguments = parser.parse_args(argv)
    command = arguments.command[1:] if arguments.command[:1] == ["--"] else arguments.command
    if not command or not arguments.window.strip():
        parser.error("supply a command after -- and a nonempty --window")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", arguments.name):
        parser.error("--name must contain only letters, digits, underscores and hyphens")
    build = Path(arguments.build).resolve()
    if not (build / "bin").is_dir():
        print(f"no build at {build}")
        return 1
    size = (arguments.width, arguments.height)
    case = Case(
        game=arguments.name,
        presentation=arguments.presentation,
        program=command[0],
        arguments=tuple(command[1:]),
        expected=arguments.presentation,
        acted=arguments.presentation != "windowed" if arguments.acted is None else arguments.acted,
        window=arguments.window,
    )
    # Kept rather than removed: a case that did not present as expected is
    # answered by its session log, and a harness that deletes the evidence of
    # its own failures is one nobody can act on.
    directory = Path(tempfile.mkdtemp(prefix="presentations-", dir=build))
    screen = Screen(size, arguments.scale, arguments.seconds)
    rows = [run_case(case, directory, build, screen)]
    outcome = report(rows)
    print(f"\nsessions under {directory}")
    return outcome


if __name__ == "__main__":
    sys.exit(main())
