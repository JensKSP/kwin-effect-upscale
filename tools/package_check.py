#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: install the package, check it in the session, remove it.

    package_check.py --package <file> --report <file>

Run as root in a machine made by tools/package-vm.py, where SDDM keeps the user
tester logged in to a Plasma Wayland session and logs them in again when the
session ends. Nothing of the effect is configured: what is checked is what a
person gets by installing the package with the system's package manager and
logging in again. Each step is recorded with what it found; a step whose
premise failed is recorded as not run, never as passed.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable

USER = "tester"
UID = 1000
SESSION = {
    "XDG_RUNTIME_DIR": f"/run/user/{UID}",
    "DBUS_SESSION_BUS_ADDRESS": f"unix:path=/run/user/{UID}/bus",
}


# What is checked, in order. A step whose premise failed stays "not run".
STEPS = (
    "a Plasma Wayland session before installing",
    "installed with the package manager",
    "a new session after logging in again",
    "the effect loaded and supported, nothing configured",
    "the settings module opens",
    "X11 goes through the session proxy",
    "SuperTuxKart on Wayland enlarged from a smaller buffer",
    "Extreme Tux Racer's X11 connection answered by the proxy",
    "removed with the package manager, KWin still running",
)


NAME = "kwin-effect-upscale"
# Each system's package manager: how it installs a package file with the
# dependencies from the system's repositories, and how it removes the package.
MANAGERS = {
    "apt-get": (("apt-get", "install", "-y"), ("apt-get", "remove", "-y", NAME)),
    "dnf": (("dnf", "install", "-y"), ("dnf", "remove", "-y", NAME)),
    "zypper": (
        ("zypper", "--non-interactive", "install", "--allow-unsigned-rpm"),
        ("zypper", "--non-interactive", "remove", NAME),
    ),
    "pacman": (("pacman", "-U", "--noconfirm"), ("pacman", "-R", "--noconfirm", NAME)),
}


def manager() -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Find the system's package manager."""
    for program, commands in MANAGERS.items():
        if shutil.which(program):
            return commands
    message = f"none of {', '.join(MANAGERS)} is installed"
    raise RuntimeError(message)


# SuperTuxKart's own settings as a player at a 4K screen has them.
GAME = """<?xml version="1.0"?>
<stkconfig version="8" >
    <Video real_width="3840" real_height="2160" fullscreen="true" render_driver="gl" />
</stkconfig>
"""


@dataclass
class Step:
    """One thing checked, and what came of it."""

    name: str
    outcome: str = "not run"
    detail: str = ""


def as_user(*command: str, environment: dict[str, str] | None = None) -> list[str]:
    """Name a command run as the tester, in their session."""
    variables = [f"{key}={value}" for key, value in (SESSION | (environment or {})).items()]
    return ["runuser", "-u", USER, "--", "env", *variables, *command]


def output(command: list[str], timeout: float = 60, input_text: str | None = None) -> str:
    """Run a command and return what it printed, or raise with what it said."""
    done = subprocess.run(
        command, input=input_text, capture_output=True, text=True, timeout=timeout, check=False
    )
    if done.returncode:
        message = f"{command[0]} failed: {(done.stderr or done.stdout).strip()[-400:]}"
        raise RuntimeError(message)
    return done.stdout


def kwin() -> str:
    """Name the tester's compositor process, or nothing."""
    found = subprocess.run(
        ["pgrep", "-u", USER, "-x", "kwin_wayland"], capture_output=True, text=True, check=False
    ).stdout.split()
    return found[0] if found else ""


def effects(method: str, argument: str) -> str:
    """Ask KWin's effects interface one question in the tester's session."""
    answer = output(
        as_user(
            *("busctl", "--user", "--json=short", "call", "org.kde.KWin", "/Effects"),
            *("org.kde.kwin.Effects", method, "s", argument),
        )
    )
    return str(json.loads(answer)["data"][0])


def metrics() -> dict[str, str]:
    """Read the effect's line for programs."""
    for line in effects("supportInformation", "upscale").splitlines():
        if line.startswith("metrics: "):
            return dict(entry.partition("=")[::2] for entry in line.split()[1:] if "=" in entry)
    return {}


def session_environment() -> dict[str, str]:
    """Read the environment the tester's session publishes to its programs."""
    lines = output(as_user("systemctl", "--user", "show-environment")).splitlines()
    return dict(line.partition("=")[::2] for line in lines if "=" in line)


def journal(since: str) -> str:
    """Read the tester's own log since a moment."""
    return output(["journalctl", f"_UID={UID}", "--since", since, "--no-pager", "-o", "cat"])


def relogin(previous: str) -> str:
    """Log the tester out and in again, and wait for the new Plasma session.

    Plasma runs as units of the tester's own service manager, which their
    SSH session keeps running, so ending SDDM's session left the old KWin in
    place and the new login met it (2026-09-29). What a logout ends goes here:
    SDDM stops, the tester's service manager restarts with nothing of the old
    session in it, and SDDM logs the tester in anew, as at boot. The SSH
    session the check runs in is a scope of its own and stays.
    """
    subprocess.run(["systemctl", "stop", "sddm"], check=False)
    subprocess.run(["systemctl", "restart", f"user@{UID}.service"], check=False)
    gone = time.monotonic() + 60
    while kwin() and time.monotonic() < gone:
        time.sleep(1)
    subprocess.run(["systemctl", "start", "sddm"], check=False)
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline:
        current = kwin()
        if current and current != previous:
            try:
                effects("isEffectLoaded", "upscale")
            except RuntimeError:
                pass
            else:
                return current
        time.sleep(3)
    message = "no new Plasma session within five minutes"
    raise RuntimeError(message)


def watch(
    program: list[str], environment: dict[str, str], seconds: float, seen: Callable[[], str]
) -> str:
    """Run a program in the session until something is seen, and say what."""
    running = subprocess.Popen(
        as_user(*program, environment=environment),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and running.poll() is None:
            if found := seen():
                return found
            time.sleep(2)
    finally:
        running.terminate()
        try:
            running.wait(timeout=15)
        except subprocess.TimeoutExpired:
            running.kill()
    return ""


def enlarged_game() -> str:
    """Say how the effect enlarges SuperTuxKart, if it does."""
    reading = metrics()
    supplied, destination = reading.get("supplied", ""), reading.get("destination", "")
    if (
        "supertuxkart" in reading.get("window", "")
        and reading.get("selected") == "1"
        and reading.get("scaling") == "1"
        and supplied
        and supplied != destination
    ):
        return f"{supplied} enlarged to {destination}, {reading.get('presentation')}"
    return ""


def check(package: str, steps: list[Step]) -> None:  # noqa: PLR0915 - One step after another.
    """Carry out the steps in order, each only where the ones it needs passed."""

    def begin(name: str) -> Step:
        return next(step for step in steps if step.name == name)

    step = begin("a Plasma Wayland session before installing")
    first = kwin()
    step.outcome, step.detail = ("passed", f"kwin_wayland {first}") if first else ("failed", "")
    step = begin("installed with the package manager")
    install, remove = manager()
    try:
        output([*install, package], timeout=1800)
        step.outcome = "passed"
    except RuntimeError as error:
        step.outcome, step.detail = "failed", str(error)
        return
    step = begin("a new session after logging in again")
    since = time.strftime("%Y-%m-%d %H:%M:%S")
    try:
        current = relogin(first)
        step.outcome, step.detail = "passed", f"kwin_wayland {current}"
    except RuntimeError as error:
        step.outcome, step.detail = "failed", str(error)
        return
    step = begin("the effect loaded and supported, nothing configured")
    loaded, supported = (
        effects("isEffectLoaded", "upscale"),
        effects("isEffectSupported", "upscale"),
    )
    step.outcome = "passed" if loaded == supported == "True" else "failed"
    step.detail = f"loaded {loaded}, supported {supported}"
    step = begin("the settings module opens")
    opened = subprocess.run(
        as_user(
            *("timeout", "20", "kcmshell6", "kwin/effects/configs/kwin_upscale_config"),
            environment={"QT_QPA_PLATFORM": "offscreen"},
        ),
        capture_output=True,
        text=True,
        check=False,
    )
    # Still open when the timeout ended it is what opening means here.
    step.outcome = "passed" if opened.returncode == 124 else "failed"  # noqa: PLR2004
    step.detail = f"exit {opened.returncode}; {opened.stderr.strip()[-200:]}"
    environment = session_environment()
    step = begin("X11 goes through the session proxy")
    routed = environment.get("UPSCALE_X11_SESSION_ROUTED") == "1"
    started = "Upscale X11 backend started" in journal(since)
    step.outcome = "passed" if routed and started else "failed"
    step.detail = f"routed {routed}, proxy started {started}"
    step = begin("SuperTuxKart on Wayland enlarged from a smaller buffer")
    # Programs start with the environment the session publishes, as they do
    # from its launcher: its PATH has /usr/games, where Debian puts games.
    # The game itself is set up as a player at this screen has it: fullscreen
    # at 3840 x 2160, where a fresh configuration would draw 1024 x 768, which
    # the effect rightly leaves alone as under half the screen.
    configuration = f"/home/{USER}/.config/supertuxkart/config-0.10"
    output(as_user("mkdir", "-p", configuration))
    output(as_user("sh", "-c", f"cat > {configuration}/config.xml"), input_text=GAME)
    game = ["supertuxkart", "-R", "--track=lighthouse", "--numkarts=1"]
    found = watch(game, environment | {"SDL_VIDEODRIVER": "wayland"}, 300, enlarged_game)
    step.outcome, step.detail = ("passed", found) if found else ("failed", str(metrics()))
    step = begin("Extreme Tux Racer's X11 connection answered by the proxy")

    def answered() -> str:
        return next(
            (
                line
                for line in journal(since).splitlines()
                if "Upscale X11 connection" in line and "extremetuxracer" in line
            ),
            "",
        )

    found = watch(["etr"], environment, 120, answered)
    step.outcome, step.detail = ("passed", found) if found else ("failed", "no connection logged")
    step = begin("removed with the package manager, KWin still running")
    try:
        output(list(remove), timeout=600)
    except RuntimeError as error:
        step.outcome, step.detail = "failed", str(error)
        return
    time.sleep(5)
    after = kwin()
    try:
        effects("isEffectLoaded", "upscale")
        answering = True
    except RuntimeError:
        answering = False
    step.outcome = "passed" if after == current and answering else "failed"
    step.detail = f"kwin_wayland {after or 'gone'}, answering {answering}"


def main(argv: list[str] | None = None) -> int:
    """Run the check and write its report."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True)
    parser.add_argument("--report", required=True)
    arguments = parser.parse_args(argv)
    steps = [Step(name) for name in STEPS]
    try:
        check(arguments.package, steps)
    finally:
        with open(arguments.report, "w") as report:  # noqa: PTH123
            json.dump([asdict(step) for step in steps], report, indent=2)
    for step in steps:
        print(f"{step.outcome:8} {step.name}: {step.detail}")
    return 0 if all(step.outcome == "passed" for step in steps) else 1


if __name__ == "__main__":
    sys.exit(main())
