#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure a release against the display server it is placed in front of.

The effect changes what a compositor shows and what a display server tells its
clients, so anything it breaks there it breaks for every program on the
machine. A release is therefore compared against the Xwayland and KWin it sits
on rather than against a score: neither suite passes completely on a stock
system, and a release is not asked to do better than the system it runs on.
What it may not do is turn a case that passed without it into one that fails
with it.

Each suite is run twice against the same everything else. The baseline arm is
the system alone. The upscaled arm puts this release's transport in front of
the same Xwayland and has the effect act on the suite's own clients, which are
selected by the path they were started from - which is why the image carries
the suite twice, at two paths, built from one pinned commit.
"""

from __future__ import annotations

import argparse
import bz2
import contextlib
import json
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

# The resolution preset both arms carry, by the number the configuration
# stores. It is the same in each: what separates them is whether the effect
# reaches the suite's programs at all, not what it would ask them for.
PERFORMANCE = 4

# Where the compositor looks for an effect on a machine it is installed on.
# The gate measures the release the way it is installed rather than the way it
# is built, because that is the thing a person runs.
EFFECTS = Path("/usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins")

# Where the image puts the two builds of the suite. The upscaled arm names its
# own tree so the effect can tell that arm's clients from the other's.
BASELINE_TREE = Path("/opt/xts")
UPSCALED_TREE = Path("/opt/xts-upscaled")

# What piglit is asked to run for each suite this gate covers.
SUITES = {
    "xts": ["-x", "^rendercheck", "xts"],
    "render": ["-t", "^rendercheck", "xts"],
    "glx": ["-p", "glx", "-t", "^glx", "all"],
}


@dataclass(frozen=True)
class Run:
    """What a pair is run against: one build, one screen, one time budget."""

    build: Path
    suite: str
    size: tuple[int, int]
    minutes: int


@dataclass(frozen=True)
class Arm:
    """One half of a pair: the same suite, with and without this release."""

    name: str
    tree: Path
    upscaled: bool


ARMS = (
    Arm(name="present", tree=BASELINE_TREE, upscaled=False),
    Arm(name="scaling", tree=BASELINE_TREE, upscaled=True),
)


def write_session(root: Path, arm: Arm) -> dict[str, str]:
    """Lay out one arm's private configuration and say how to reach it."""
    config = root / "config"
    config.mkdir(parents=True)
    # The release is loaded, enabled and live in both arms, and one setting
    # separates them: whether it acts on programs no profile names. Arm one
    # therefore answers "does this effect trouble anything merely by running",
    # and arm two "does scaling trouble anything". A baseline without the
    # plugin would answer neither, because every difference could be charged
    # to the plugin being there at all.
    #
    # Unlisted applications is what reaches a suite: it is thousands of
    # programs nobody wrote a profile for, and naming them by the path they
    # start from would cover only the ones somebody remembered.
    (config / "kwinrc").write_text(
        "[Plugins]\nupscaleEnabled=true\n"
        "[Effect-upscale]\n"
        "Enabled=true\n"
        "X11Proxy=true\n"
        f"UnlistedApplications={'true' if arm.upscaled else 'false'}\n"
        f"Resolution={PERFORMANCE}\n"
        "MethodX11FullScreen=Auto\nMethodX11Borderless=Auto\n"
        "MinimumPixels=0\nSharpening=false\nOsd=false\n"
    )
    (config / "piglit.conf").write_text(f"[xts]\npath={arm.tree}\n")
    # A session here is started by hand, not by the init system, and asking for
    # a systemd boot in a container fails part-way and leaves the compositor
    # without the arguments that give it a screen. The splash is switched off
    # because nothing watches it and it holds the session in composition.
    (config / "startkderc").write_text("[General]\nsystemdBoot=false\n")
    (config / "ksplashrc").write_text("[KSplash]\nEngine=none\n")
    return {
        "XDG_CONFIG_HOME": str(config),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "PIGLIT_CONFIG": str(config / "piglit.conf"),
        "LIBGL_ALWAYS_SOFTWARE": "1",
        "LC_ALL": "C.UTF-8",
        # The transport reads at startup whether this session routes through
        # it. Without this it execs the stock server and the arm quietly
        # becomes a second baseline, which is the one way a pair can pass
        # while measuring nothing at all.
        "UPSCALE_X11_SESSION_ROUTED": "1" if arm.upscaled else "0",
    }


def inner_script(root: Path, suite: str, minutes: int, *, upscaled: bool) -> Path:
    """Write the command the compositor runs: the suite, inside its own session."""
    results = root / "piglit"
    arguments = [
        "piglit",
        "run",
        "-f",
        str(root / "config" / "piglit.conf"),
        "-1",
        "--timeout",
        "15",
        "-l",
        "quiet",
        *SUITES[suite],
        str(results),
    ]
    # The effect is switched on in the configuration rather than loaded on
    # demand. It is built as one of the compositor's own effects, and those are
    # enabled when the compositor starts rather than asked for over the bus:
    # loadEffect answers false for one however well it is installed. What is
    # checked instead is that it actually came up, which the transport says by
    # answering a connection.
    load = (
        'echo "effects: $(qdbus6 org.kde.KWin /Effects '
        'org.kde.kwin.Effects.loadedEffects 2>/dev/null | tr "\\n" " ")" >&2\n'
        if upscaled
        else ""
    )
    script = root / "inside.sh"
    script.write_text(
        "#!/bin/sh\n"
        "# The suite runs inside the session because the display it tests is\n"
        "# the session's own, and in the upscaled arm the transport in front of\n"
        "# it is reached through DISPLAY like any other client reaches it.\n"
        "sleep 8\n" + load + f"timeout {minutes * 60} {shlex.join(arguments)}\n"
        "echo $? > " + shlex.quote(str(root / "exit-code")) + "\n"
    )
    script.chmod(0o700)
    return script


def run_arm(arm: Arm, root: Path, run: Run) -> dict[str, object]:
    """Run one arm to completion and return the cases it produced.

    The suite runs inside the session, because the display it tests is the
    session's own and the transport in front of it is reached through DISPLAY
    the way any other client reaches it.
    """
    cell = root / arm.name
    cell.mkdir(parents=True)
    environment = write_session(cell, arm)
    inner = inner_script(cell, run.suite, run.minutes, upscaled=arm.upscaled)
    binaries = cell / "bin"
    binaries.mkdir()
    # Installed for both arms: the release is present in each and only its
    # settings differ.
    shutil.copyfile(
        run.build / "bin" / "kwin" / "effects" / "plugins" / "upscale.so", EFFECTS / "upscale.so"
    )
    packaged = shutil.which("kwin_wayland")
    if not packaged:
        return {"arm": arm.name, "root": str(cell), "cases": {}, "engaged": False}
    # The session starts its own compositor, so the screen it is to run on is
    # given through a compositor of this name found first on the path. The
    # packaged binary carries file capabilities a container will not exec, so
    # what the wrapper runs is a copy of it.
    private = binaries / "kwin_wayland.real"
    shutil.copyfile(packaged, private)
    private.chmod(0o700)
    wrapper = binaries / "kwin_wayland"
    wrapper.write_text(
        "#!/bin/sh\n"
        # Says so when it runs: which layer starts the compositor decides where
        # this run's screen has to be given, and a log that answers that costs
        # nothing beside a failure that does not.
        'echo "conformance shim: kwin_wayland ran" >&2\n'
        f"exec {shlex.quote(str(private))} --virtual --output-count 1 "
        f"--width {run.size[0]} --height {run.size[1]} "
        '--no-lockscreen --no-global-shortcuts --no-kactivities "$@"\n'
    )
    wrapper.chmod(0o700)
    if arm.upscaled:
        # KWin finds Xwayland through the path as well. The transport
        # supervises the stock server rather than replacing it, so both arms
        # run the very same Xwayland.
        (binaries / "Xwayland").symlink_to(run.build / "bin" / "Xwayland")
    session = {**os.environ, **environment}
    session["PATH"] = f"{binaries}:{os.environ.get('PATH', '/usr/bin:/bin')}"
    with (cell / "session.log").open("w") as log:
        # Through the wrapper a session would use, directly rather than
        # through a whole session: a session starts the compositor by absolute
        # path, so the screen this run gives it could never be handed over -
        # measured 2026-09-27, neither shim was reached. The wrapper does look
        # its compositor up on the path, which is how the screen arrives.
        started = subprocess.Popen(
            [
                "dbus-run-session",
                "--",
                "kwin_wayland_wrapper",
                "--xwayland",
                "--exit-with-session",
                str(inner),
            ],
            env=session,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        wait_for_arm(started, cell / "exit-code", run.minutes)
    return {
        "arm": arm.name,
        "root": str(cell),
        "cases": read_cases(cell / "piglit"),
        "engaged": engaged(arm, (cell / "session.log").read_text(errors="replace")),
    }


def wait_for_arm(started: subprocess.Popen[bytes], marker: Path, minutes: int) -> None:
    """Wait for the suite to finish, then take the whole session down.

    A session does not end because the program inside it did, so what is waited
    for is the suite's own mark rather than the session's exit.
    """
    deadline = time.monotonic() + minutes * 60 + 600
    while time.monotonic() < deadline and started.poll() is None and not marker.exists():
        time.sleep(1.0)
    with contextlib.suppress(ProcessLookupError):
        os.killpg(started.pid, signal.SIGTERM)
    try:
        started.wait(timeout=30)
    except subprocess.TimeoutExpired:
        with contextlib.suppress(ProcessLookupError):
            os.killpg(started.pid, signal.SIGKILL)
        started.wait(timeout=10)


def engaged(arm: Arm, log: str) -> bool:
    """Whether this arm did what its name says, rather than the other arm's job.

    Both arms run the release: the transport is in front of the same Xwayland
    in each, and an arm that fell back to the stock server is not the arm it
    claims to be. They are told apart by what the transport then did. The one
    that only runs must advertise nothing, because no profile names the suite's
    programs; the one that scales must advertise, because it names them all.
    An arm that scaled nothing would agree with the other case for case and
    report no regression, having compared a system against itself.
    """
    routed = "Upscale X11 backend started" in log
    bypassed = "executing stock Xwayland directly" in log
    advertised = 'reason= "connection display advertisement"' in log
    return routed and not bypassed and advertised == arm.upscaled


def read_cases(results: Path) -> dict[str, str]:
    """Read the outcome piglit recorded for every case it completed."""
    packed = results / "results.json.bz2"
    plain = results / "results.json"
    if packed.exists():
        with bz2.open(packed, "rt") as stream:
            return {name: item["result"] for name, item in json.load(stream)["tests"].items()}
    if plain.exists():
        return {
            name: item["result"] for name, item in json.loads(plain.read_text())["tests"].items()
        }
    return {}


def compare(baseline: dict[str, str], upscaled: dict[str, str]) -> dict[str, object]:
    """Say what this release did to the system underneath it, case by case.

    Only a case that the system completed can say anything about the release.
    One that fails in both arms is the system's and is reported as such; one
    that passes without this release and fails with it is the release's, and
    it is what the gate exists to catch.
    """
    passing = {"pass", "warn"}
    shared = baseline.keys() & upscaled.keys()
    regressed = sorted(
        name for name in shared if baseline[name] in passing and upscaled[name] not in passing
    )
    repaired = sorted(
        name for name in shared if baseline[name] not in passing and upscaled[name] in passing
    )
    return {
        "completed": {
            "baseline": len(baseline),
            "upscaled": len(upscaled),
            "compared": len(shared),
        },
        "missing": sorted(baseline.keys() - upscaled.keys()),
        "added": sorted(upscaled.keys() - baseline.keys()),
        "regressed": regressed,
        "repaired": repaired,
    }


def report(suite: str, verdict: dict[str, object]) -> int:
    """Say what the pair showed, and fail only on what this release caused."""
    counts = verdict["completed"]
    print(
        f"{suite}: {counts['baseline']} cases without this release, "
        f"{counts['upscaled']} with it, {counts['compared']} comparable"
    )
    for name in verdict["missing"][:20]:
        print(f"  only not acting: {name}")
    for name in verdict["added"][:20]:
        print(f"  only scaling:     {name}")
    for name in verdict["repaired"][:20]:
        print(f"  repaired:     {name}")
    for name in verdict["regressed"]:
        print(f"  REGRESSED:    {name}")
    if verdict["regressed"]:
        print(
            f"\n{len(verdict['regressed'])} cases pass with the effect off and fail with it scaling"
        )
        return 1
    if not counts["compared"]:
        print("\nno case completed in both arms; nothing was compared")
        return 1
    print("\nno case that passes with the effect merely running fails with it scaling")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Run a suite in both arms and compare them."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=sorted(SUITES), default="xts")
    parser.add_argument(
        "--build", default="build/gcc", help="the build whose effect and transport are measured"
    )
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--minutes", type=int, default=90, help="how long one arm gets")
    parser.add_argument("--out", default="", help="where the two arms are kept")
    arguments = parser.parse_args(argv)
    build = Path(arguments.build).resolve()
    if not (build / "bin" / "Xwayland").exists():
        print(f"no transport at {build}/bin/Xwayland")
        return 1
    if not BASELINE_TREE.exists() or not UPSCALED_TREE.exists():
        print("the suites are missing; run this in the conformance image")
        return 1
    root = (
        Path(arguments.out)
        if arguments.out
        else Path(tempfile.mkdtemp(prefix="conformance-", dir=build))
    )
    root.mkdir(parents=True, exist_ok=True)
    size = (arguments.width, arguments.height)
    run = Run(build=build, suite=arguments.suite, size=size, minutes=arguments.minutes)
    produced = {arm.name: run_arm(arm, root, run) for arm in ARMS}
    # An arm that did not do its own job cannot be compared against the other.
    # Reporting that as "no regression" would be the most expensive kind of
    # green: a gate that passes because it measured nothing.
    idle = [name for name, arm in produced.items() if not arm["engaged"]]
    if idle:
        for name in idle:
            print(f"{name}: did not run as itself; see {produced[name]['root']}/session.log")
        print("\nthe pair was not comparable, so nothing was measured")
        return 1
    verdict = compare(produced["present"]["cases"], produced["scaling"]["cases"])
    (root / "verdict.json").write_text(json.dumps(verdict, indent=2))
    outcome = report(arguments.suite, verdict)
    print(f"arms under {root}")
    return outcome


if __name__ == "__main__":
    sys.exit(main())
