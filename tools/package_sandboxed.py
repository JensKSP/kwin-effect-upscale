#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: the shipped games as Flatpak and as Snap.

    package_sandboxed.py --package FILE --report FILE

The same program under another path, in another process namespace, over
another socket: what that does to the names the effect and the proxy give a
program, and whether the shipped entries still claim it. SuperTuxKart and
Extreme Tux Racer are taken from Flathub and the Snap Store, each sandbox
where the system has it or its package manager offers it, and run on Wayland
and through X11, after the system's own packages of them for comparison. Run
as root in a machine made by tools/package-vm.py, like the package check,
whose session helpers this uses; the package is removed at the end either way.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

import package_check as pc

FLATHUB = "https://dl.flathub.org/repo/flathub.flatpakrepo"
KART = "net.supertuxkart.SuperTuxKart"
RACER = "net.sourceforge.ExtremeTuxRacer"
# The snaps of the same two games, as the Snap Store named them on 2026-09-27.
SNAPS = ("supertuxkart", "extreme-tux-racer")
# The games' processes, in or out of a sandbox.
GAMES = ("supertuxkart", "etr")
# Part of the racer's window class, "Extreme Tux Racer 0.8.4", which carries
# its version; KWin's window runner takes one word of it.
RACER_CLASS = "Racer"
HOME = Path("/home") / pc.USER


@dataclass(frozen=True)
class Case:
    """One game in one sandbox on one route, and what should claim it."""

    name: str
    command: tuple[str, ...]
    # The class KWin's window runner finds the window by.
    window: str
    # The shipped entry the proxy should answer for, on the X11 route.
    profile: str
    x11: bool
    environment: dict[str, str] = field(default_factory=dict)
    # Where SuperTuxKart keeps its settings in this sandbox; none for the racer.
    settings: Path | None = None


@dataclass
class Seen:
    """What one case showed, filled as it runs."""

    name: str
    outcome: str = "not run"
    answer: str = ""
    enlarged: str = ""
    programs: list[str] = field(default_factory=list)
    metrics: dict[str, str] = field(default_factory=dict)
    # The effect's status at the last look, which says why it does not act.
    status: list[str] = field(default_factory=list)
    detail: str = ""


def native_cases() -> list[Case]:
    """Name the same three as the system packages them, to compare the sandboxes with."""
    race = tuple(pc.RACE[1:])
    settings = HOME / ".config/supertuxkart/config-0.10"
    return [
        Case(
            "Native SuperTuxKart on Wayland",
            ("supertuxkart", *race),
            "supertuxkart",
            "supertuxkart",
            x11=False,
            environment={"SDL_VIDEODRIVER": "wayland"},
            settings=settings,
        ),
        Case(
            "Native SuperTuxKart through X11",
            ("supertuxkart", *race),
            "supertuxkart",
            "supertuxkart",
            x11=True,
            environment={"SDL_VIDEODRIVER": "x11"},
            settings=settings,
        ),
        Case(
            "Native Extreme Tux Racer through X11",
            ("etr",),
            RACER_CLASS,
            "extremetuxracer",
            x11=True,
        ),
    ]


def provide_native(install: tuple[str, ...]) -> str:
    """Find both games where the system packages them, as the machine's template installs them."""
    del install
    found = [shutil.which(game, path="/usr/games:/usr/bin") for game in ("supertuxkart", "etr")]
    if not all(found):
        message = "the system packages neither game here"
        raise RuntimeError(message)
    return ", ".join(str(game) for game in found)


def flatpak_cases() -> list[Case]:
    """SuperTuxKart on Wayland and through X11, and the racer, which has only X11."""
    race = tuple(pc.RACE[1:])
    settings = HOME / ".var/app" / KART / "config/supertuxkart/config-0.10"
    return [
        Case(
            "Flatpak SuperTuxKart on Wayland",
            ("flatpak", "run", "--env=SDL_VIDEODRIVER=wayland", KART, *race),
            "supertuxkart",
            "supertuxkart",
            x11=False,
            settings=settings,
        ),
        # The Flatpak grants X11 only as a fallback, so its socket is granted
        # and Wayland's taken away for the X11 route.
        Case(
            "Flatpak SuperTuxKart through X11",
            (
                "flatpak",
                "run",
                "--socket=x11",
                "--nosocket=wayland",
                "--env=SDL_VIDEODRIVER=x11",
                KART,
                *race,
            ),
            "supertuxkart",
            "supertuxkart",
            x11=True,
            settings=settings,
        ),
        Case(
            "Flatpak Extreme Tux Racer through X11",
            ("flatpak", "run", RACER),
            RACER_CLASS,
            "extremetuxracer",
            x11=True,
        ),
    ]


def snap_cases() -> list[Case]:
    """Name the same three with the snaps' programs, as /snap/bin names them."""
    race = tuple(pc.RACE[1:])
    settings = HOME / "snap/supertuxkart/current/.config/supertuxkart/config-0.10"
    kart = "/snap/bin/supertuxkart"
    return [
        Case(
            "Snap SuperTuxKart on Wayland",
            (kart, *race),
            "supertuxkart",
            "supertuxkart",
            x11=False,
            environment={"SDL_VIDEODRIVER": "wayland"},
            settings=settings,
        ),
        Case(
            "Snap SuperTuxKart through X11",
            (kart, *race),
            "supertuxkart",
            "supertuxkart",
            x11=True,
            environment={"SDL_VIDEODRIVER": "x11"},
            settings=settings,
        ),
        Case(
            "Snap Extreme Tux Racer through X11",
            (snap_program(SNAPS[1]),),
            RACER_CLASS,
            "extremetuxracer",
            x11=True,
        ),
    ]


def snap_program(snap: str, directory: Path = Path("/snap/bin")) -> str:
    """Name the program a snap puts in /snap/bin: its own name, or its first application."""
    programs = sorted(directory.glob(f"{snap}*"))
    exact = [program for program in programs if program.name == snap]
    return str((exact or programs or [directory / snap])[0])


def provide_flatpak(install: tuple[str, ...]) -> str:
    """Install Flatpak where it is missing, and both games from Flathub, for every user."""
    if not shutil.which("flatpak"):
        pc.output([*install, "flatpak"], timeout=1800)
    pc.output(["flatpak", "remote-add", "--if-not-exists", "flathub", FLATHUB], timeout=600)
    pc.output(
        ["flatpak", "install", "-y", "--noninteractive", "flathub", KART, RACER], timeout=3600
    )
    return pc.output(["flatpak", "--version"]).strip()


def provide_snap(install: tuple[str, ...]) -> str:
    """Install snapd where it is missing, and both games from the Snap Store."""
    if not shutil.which("snap"):
        pc.output([*install, "snapd"], timeout=1800)
    pc.output(["snap", "wait", "system", "seed.loaded"], timeout=1800)
    for snap in SNAPS:
        pc.output(["snap", "install", snap], timeout=3600)
    # Its first start makes the snap's directory in the tester's home, where
    # its settings go.
    subprocess.run(
        pc.as_user("timeout", "120", "/snap/bin/supertuxkart", "--version"),
        capture_output=True,
        check=False,
    )
    return pc.output(["snap", "version"]).splitlines()[0].strip()


def window_ids(window: str) -> list[str]:
    """Find the windows of a class through KWin's window runner, by their KWin IDs."""
    answer = pc.output(
        pc.as_user(
            *("busctl", "--user", "--json=short", "call", "org.kde.KWin", "/WindowsRunner"),
            *("org.kde.krunner1", "Match", "s", f"window appname={window}"),
        )
    )
    return [match[0].split("_", 1)[1] for match in json.loads(answer)["data"][0]]


def program_of(window: str) -> str:
    """Ask the effect how it names the program behind a window, as Add from Window does."""
    answer = pc.output(
        pc.as_user(
            *("busctl", "--user", "--json=short", "call", "org.kde.KWin"),
            *("/org/kde/KWin/Effect/Upscale1", "org.kde.KWin.Effect.Upscale1"),
            *("executablePath", "s", window),
        )
    )
    return str(json.loads(answer)["data"][0])


def status_lines() -> list[str]:
    """Read the effect's status, the lines a person reads, without the metrics line."""
    information = pc.effects("supportInformation", "upscale").splitlines()
    return [line for line in information if line and not line.startswith("metrics: ")][:24]


def desktop_scales() -> list[str]:
    """Read the scale of each output, as KWin's own support information gives it."""
    information = pc.output(
        pc.as_user(
            *("busctl", "--user", "--json=short", "call", "org.kde.KWin", "/KWin"),
            *("org.kde.KWin", "supportInformation"),
        )
    )
    lines = str(json.loads(information)["data"][0]).splitlines()
    return [line.strip() for line in lines if line.strip().startswith("Scale:")]


def enlarged(reading: dict[str, str]) -> str:
    """Say how the effect enlarges the window it follows, if it does: either game, fullscreen."""
    supplied, destination = reading.get("supplied", ""), reading.get("destination", "")
    if (
        reading.get("selected") == reading.get("scaling") == "1"
        and supplied
        and supplied != destination
    ):
        return f"{supplied} enlarged to {destination}, {reading.get('presentation')}"
    return ""


def answer_for(profile: str, since: str) -> str:
    """Find the proxy's answer for a connection it told the smaller screen, with its names."""
    return next(
        (
            line
            for line in pc.journal(since).splitlines()
            if "Upscale X11 connection" in line
            and f'"{profile}"' in line
            and "connection display advertisement" in line
        ),
        "",
    )


def run_case(case: Case, session: pc.Session, seen: Seen) -> None:
    """Run the game until the effect claims it, and record what named it."""
    if case.settings:
        pc.output(pc.as_user("mkdir", "-p", str(case.settings)))
        pc.output(pc.as_user("sh", "-c", f"cat > {case.settings}/config.xml"), input_text=pc.GAME)
    stop_games()
    pc.settle(session.kwin)
    since = time.strftime("%Y-%m-%d %H:%M:%S")
    route = "x11" if case.x11 else "wayland"

    def done() -> str:
        seen.metrics = pc.metrics()
        seen.status = status_lines()
        if case.x11 and not seen.answer:
            seen.answer = answer_for(case.profile, since)
        if not seen.enlarged and seen.metrics.get("windowsystem") == route:
            seen.enlarged = enlarged(seen.metrics)
        windows = window_ids(case.window)
        if windows and not seen.programs:
            seen.programs = [program_of(window) for window in windows]
        complete = (seen.answer or not case.x11) and seen.enlarged
        return "done" if complete and seen.programs else ""

    environment = session.environment | case.environment
    finished = pc.watch(list(case.command), environment, 300, done)
    seen.outcome = "passed" if finished else "failed"


def stop_games() -> None:
    """End whatever a game left running before the next starts.

    Ending the command that started a game ends neither Flatpak's sandbox nor
    the game in it, and the tester's instances are not root's to list.
    """
    for game in GAMES:
        subprocess.run(["pkill", "--signal", "KILL", "-u", pc.USER, "-x", game], check=False)
    time.sleep(5)


def sandboxed(package: str, result: dict[str, object]) -> None:
    """Provide the sandboxes, install the package, log in again, and run each case."""
    install, _ = pc.manager()
    cases: list[Case] = []
    sandboxes: dict[str, str] = {}
    for name, provide, made in (
        ("native", provide_native, native_cases),
        ("flatpak", provide_flatpak, flatpak_cases),
        ("snap", provide_snap, snap_cases),
    ):
        try:
            sandboxes[name] = provide(install)
            cases += made()
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            sandboxes[name] = f"not available: {error}"
    result["sandboxes"] = sandboxes
    pc.output([*install, package], timeout=1800)
    current = pc.relogin(pc.kwin())
    session = pc.Session(time.strftime("%Y-%m-%d %H:%M:%S"), current)
    session.environment = pc.session_environment()
    result["desktop scales"] = desktop_scales()
    seen = [Seen(case.name) for case in cases]
    result["cases"] = [asdict(record) for record in seen]
    for case, record in zip(cases, seen, strict=True):
        try:
            run_case(case, session, record)
        except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as error:
            record.outcome, record.detail = "failed", f"{type(error).__name__}: {error}"
        stop_games()
        result["cases"] = [asdict(each) for each in seen]
    result["passed"] = bool(seen) and all(record.outcome == "passed" for record in seen)


def main(argv: list[str] | None = None) -> int:
    """Run the cases and write their report."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--package", required=True)
    parser.add_argument("--report", required=True)
    options = parser.parse_args(argv)
    result: dict[str, object] = {"passed": False}
    try:
        sandboxed(options.package, result)
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        result["error"] = str(error)
    finally:
        _, remove = pc.manager()
        try:
            pc.output(list(remove), timeout=600)
        except RuntimeError as error:
            result["removal"] = str(error)
        Path(options.report).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(result, indent=1, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
