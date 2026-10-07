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

XTS runs on an isolated rootful Xwayland display, whose windows no window
manager touches. Three arms compare the same stock server directly, through
this release's proxy with unchanged display information, and through its proxy
with reduced display information supplied by the live effect. KWin integration
tests separately cover managed windows and rendering. Render and GLX retain
paired runs against KWin's managed display, with the effect idle and scaling.

Run it in the project VM, inside the conformance image, as an ordinary user:

    podman run --rm --user 1000 --userns=keep-id --group-add keep-groups \
        --device /dev/dri/card0 --device /dev/dri/renderD128 -v "$PWD:/src" -w /src \
        -e XDG_RUNTIME_DIR=/tmp \
        upscale-conformance:trixie \
        python3 -B tools/check-conformance.py --suite xts --build build/gcc

Use the VM's vgem primary node in place of card0 if its number differs, and
give the run user access to both nodes. Never use the desktop's GPU. The
primary and render nodes are both required: KWin's virtual backend allocates
its buffers through the primary node. It offers OpenGL compositing
only where it finds one, the effect answers that it is unsupported under any
other and is then never loaded, and a pair whose effect never came up has
compared a system against itself.
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
from typing import TypedDict

# The resolution preset both arms carry, by the number the configuration
# stores. It is the same in each: what separates them is whether the effect
# reaches the suite's programs at all, not what it would ask them for.
PERFORMANCE = 4

# Where the compositor looks for an effect on a machine it is installed on.
# The gate measures the release the way it is installed rather than the way it
# is built, because that is the thing a person runs.
EFFECTS = Path("/usr/lib/x86_64-linux-gnu/qt6/plugins/kwin/effects/plugins")

# Where the image puts the suite. Both arms run the one tree, so that the two
# halves of a pair are the very same programs at the very same paths: which arm
# acts on them is decided by the profile in that arm's configuration, not by
# where they were installed. The image's second copy is what the other route
# would have needed and is only checked for here, as the mark of an image that
# carries the suite at all.
BASELINE_TREE = Path("/opt/xts")
UPSCALED_TREE = Path("/opt/xts-upscaled")


class Counts(TypedDict):
    """How many cases each arm completed, and how many can be set side by side."""

    baseline: int
    upscaled: int
    compared: int


class Verdict(TypedDict):
    """What a pair showed: the counts, and the cases that differ between arms."""

    completed: Counts
    missing: list[str]
    added: list[str]
    regressed: list[str]
    repaired: list[str]


class ArmResult(TypedDict):
    """What one arm produced: where it ran, what it measured, and whether it was itself."""

    arm: str
    root: str
    cases: dict[str, str]
    engaged: bool
    complete: bool


@dataclass(frozen=True)
class Suite:
    """What piglit is asked to run, and where the programs it starts live."""

    arguments: tuple[str, ...]
    # A regular expression matching the executables the suite starts. The arm
    # that scales names them in a profile of its own, because that is what the
    # effect answers a connection with: an advertisement is connection-wide and
    # is given only to a program a profile names. Acting on unlisted programs
    # is a window's affair and arrives long after a client has asked what the
    # display measures. The effect anchors the expression at both ends, so it
    # has to match a whole path: written as a prefix it matched 22 of 2335
    # connections (measured 2026-09-27).
    programs: str


# What piglit is asked to run for each suite this gate covers.
SUITES = {
    "xts": Suite(("-x", "^rendercheck", "xts"), r"/opt/xts(-upscaled)?/.*"),
    "render": Suite(("-t", "^rendercheck", "xts"), r"/usr/bin/rendercheck"),
    "glx": Suite(("-p", "glx", "-t", "^glx", "all"), r"/usr/lib/[^/]+/piglit/bin/.*"),
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
    """Run the same suite with one connection and window policy."""

    name: str
    tree: Path
    upscaled: bool


ARMS = (
    Arm(name="present", tree=BASELINE_TREE, upscaled=False),
    Arm(name="scaling", tree=BASELINE_TREE, upscaled=True),
)


def write_session(root: Path, arm: Arm, run: Run, runtime: Path) -> dict[str, str]:
    """Lay out one arm's private configuration and say how to reach it."""
    config = root / "config"
    config.mkdir(parents=True)
    # XTS's outer rootful surface must stay unmanaged by the effect. The
    # scaling arm's profile still supplies real per-connection policy to XTS.
    act_on_windows = arm.upscaled and run.suite != "xts"
    (config / "kwinrc").write_text(
        "[Plugins]\nupscaleEnabled=true\n"
        "[Effect-upscale]\n"
        "Enabled=true\n"
        "X11Proxy=true\n"
        f"UnlistedApplications={'true' if act_on_windows else 'false'}\n"
        f"Resolution={PERFORMANCE}\n"
        "MethodX11FullScreen=Auto\nMethodX11Borderless=Auto\n"
        "MinimumPixels=0\nSharpening=false\nOsd=false\n"
    )
    (config / "kdeglobals").write_text("[General]\n")
    # The arm that scales names the suite's programs and nothing else does.
    # The profile is what reaches them, their connections, which is where a
    # client asks what the display measures, and their windows alike. All
    # games stays on beside it for the window rules it brings, and reaches
    # none of the suite's programs, none of which is a game. The arm that only
    # runs has no profile at all, so nothing it starts is named, which is
    # exactly the question it asks.
    if arm.upscaled:
        (config / "kwinupscalerc").write_text(
            "[Application-conformance]\n"
            "Name=Conformance suite\n"
            "Enabled=true\n"
            f"Executable={SUITES[run.suite].programs}\n"
            "ExecutableMatch=RegularExpression\n"
            f"X11ConnectionExecutable={SUITES[run.suite].programs}\n"
            "MethodX11FullScreen=Auto\n"
            "MethodX11Borderless=Auto\n"
        )
    (config / "piglit.conf").write_text(f"[xts]\npath={arm.tree}\n")
    # A session here is started by hand, not by the init system, and asking for
    # a systemd boot in a container fails part-way and leaves the compositor
    # without the arguments that give it a screen. The splash is switched off
    # because nothing watches it and it holds the session in composition.
    (config / "startkderc").write_text("[General]\nsystemdBoot=false\n")
    (config / "ksplashrc").write_text("[KSplash]\nEngine=none\n")
    return {
        # Its own, per arm: two compositors cannot share a runtime directory,
        # and the second finds the first's socket locked. Short as well,
        # because a Unix socket path may not exceed 108 bytes and a path under
        # the build directory spends most of that before a name is added.
        "XDG_RUNTIME_DIR": str(runtime),
        "XDG_CONFIG_HOME": str(config),
        "XDG_DATA_HOME": str(root / "data"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "PIGLIT_CONFIG": str(config / "piglit.conf"),
        # Which compositing the session uses is left to the compositor, and it
        # has to arrive at OpenGL: the effect answers that it is unsupported
        # under any other and is then never loaded at all. KWin's virtual
        # backend offers OpenGL only where it finds a render device, so a run
        # has to be given one - measured 2026-09-27, without it the session
        # falls back to QPainter, the effect never comes up, and the pair
        # compares a system against itself. Both arms use the same device, so
        # the comparison is still between the two arms and nothing else.
        "UPSCALE_SCREEN_WIDTH": str(run.size[0]),
        "UPSCALE_SCREEN_HEIGHT": str(run.size[1]),
        "QT_LOGGING_TO_CONSOLE": "1",
        "QT_FORCE_STDERR_LOGGING": "1",
        "LC_ALL": "C.UTF-8",
        # The transport reads at startup whether this session routes through
        # it, and both arms say yes: the release sits in front of the same
        # server in each, and one setting separates them. An arm that said no
        # would exec the stock server and quietly become a second baseline,
        # which is the one way a pair can pass while measuring nothing at all.
        "UPSCALE_X11_SESSION_ROUTED": "1",
    }


def inner_script(root: Path, arm: Arm, run: Run) -> Path:
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
        *SUITES[run.suite].arguments,
        str(results),
    ]
    # The effect is switched on in the configuration rather than loaded on
    # demand. It is built as one of the compositor's own effects, and those are
    # enabled when the compositor starts rather than asked for over the bus:
    # loadEffect answers false for one however well it is installed. So what
    # the wait is for is the compositor listing it as loaded, in both arms -
    # an arm whose effect never came up measures nothing, whichever side of
    # the pair it is, and the list it prints is what says which happened.
    script = root / "inside.sh"
    marker = shlex.quote(str(root / "exit-code"))
    on_exit = shlex.quote('status=$?; echo "$status" > ' + marker)
    if run.suite == "xts":
        arguments = [
            sys.executable,
            "-B",
            str(Path(__file__).with_name("x11_conformance.py").resolve()),
            "--root",
            str(root),
            "--build",
            str(run.build),
            "--arm",
            arm.name,
            "--size",
            f"{run.size[0]}x{run.size[1]}",
            "--",
            *arguments,
        ]
    script.write_text(
        "#!/bin/sh\n"
        "set -eu\n"
        f"trap {on_exit} 0\n"
        "# The suite runs inside the session because the display it tests is\n"
        "# the session's own, and the transport in front of it is reached\n"
        "# through DISPLAY like any other client reaches it.\n"
        "loaded=\n"
        "i=0\n"
        "while [ $i -lt 60 ]; do\n"
        "    loaded=$(qdbus6 org.kde.KWin /Effects"
        ' org.kde.kwin.Effects.loadedEffects 2>/dev/null | tr "\\n" " ")\n'
        '    case " $loaded " in *" upscale "*) break ;; esac\n'
        "    i=$((i + 1))\n"
        "    sleep 1\n"
        "done\n"
        'echo "effects: $loaded" >&2\n'
        'case " $loaded " in *" upscale "*) ;; *) exit 1 ;; esac\n'
        # Xwayland starts with the first X11 client, and the effect can answer
        # what a client asks about the display only once KWin has a connection
        # to that server itself. A suite whose own first case starts Xwayland
        # races a session still assembling itself, and loses: measured
        # 2026-09-27, every connection of such a run was answered unchanged
        # while the same program was answered with an advertisement moments
        # later. So the server is brought up here, by a client of no
        # consequence, and the suite starts against a session that is up.
        "i=0\n"
        "until xdpyinfo >/dev/null 2>&1; do\n"
        "    i=$((i + 1))\n"
        "    [ $i -gt 30 ] && exit 1\n"
        "    sleep 1\n"
        "done\n"
        "sleep 2\n" + f"timeout {run.minutes * 60} {shlex.join(arguments)}\n"
    )
    script.chmod(0o700)
    return script


def run_arm(arm: Arm, root: Path, run: Run) -> ArmResult:
    """Run one arm to completion and return the cases it produced.

    The suite runs inside the session, because the display it tests is the
    session's own and the transport in front of it is reached through DISPLAY
    the way any other client reaches it.
    """
    cell = root / arm.name
    cell.mkdir(parents=True)
    # Short, and its own for each arm: a Unix socket path may not exceed 108
    # bytes, and two compositors cannot share a runtime directory - the second
    # finds the first's socket locked and falls back to a device that is not
    # there.
    runtime = Path(
        tempfile.mkdtemp(prefix="up-", dir=os.environ.get("XDG_RUNTIME_DIR", tempfile.gettempdir()))
    )
    runtime.chmod(0o700)
    environment = write_session(cell, arm, run, runtime)
    inner = inner_script(cell, arm, run)
    binaries = cell / "bin"
    binaries.mkdir()
    # The effect supplies connection policy in each arm. XTS's bare arm uses
    # it only for the outer session, not for the nested test display.
    shutil.copyfile(
        run.build / "bin" / "kwin" / "effects" / "plugins" / "upscale.so", EFFECTS / "upscale.so"
    )
    if not shutil.which("kwin_wayland"):
        return {
            "arm": arm.name,
            "root": str(cell),
            "cases": {},
            "engaged": False,
            "complete": False,
        }
    # KWin finds Xwayland on the path, and both arms put this release's
    # transport there. It supervises the stock server rather than replacing
    # it, so the two arms measure the very same Xwayland and differ only in
    # what the effect is allowed to act on.
    (binaries / "Xwayland").symlink_to(run.build / "bin" / "Xwayland")
    session = {**os.environ, **environment}
    session["PATH"] = f"{binaries}:{os.environ.get('PATH', '/usr/bin:/bin')}"
    with (cell / "session.log").open("w") as log:
        # The compositor is the session: it loads the effects its
        # configuration enables and starts the suite itself, which is how the
        # suite's programs arrive with this session's DISPLAY. A whole Plasma
        # session was tried first and never reached the suite - measured
        # 2026-09-27, its autostart waits on a shell this container has no
        # screen for. The screen the compositor is given comes from the image;
        # see containers/conformance/Containerfile.
        started = subprocess.Popen(
            [
                "dbus-run-session",
                "--",
                "kwin_wayland",
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
        "engaged": arm_engaged(arm, cell, run.suite),
        "complete": completed(cell),
    }


def arm_engaged(arm: Arm, root: Path, suite: str) -> bool:
    """Check the outer effect and the server that actually received the tests."""
    session = (root / "session.log").read_text(errors="replace")
    if suite != "xts":
        return engaged(arm, session)
    if not engaged(Arm("present", BASELINE_TREE, upscaled=False), session):
        return False
    nested = root / "nested.log"
    if not nested.is_file():
        return False
    log = nested.read_text(errors="replace")
    routed = "Upscale X11 backend started" in log
    advertised = "connection display advertisement" in log
    bypassed = "executing stock Xwayland directly" in log
    return not bypassed and routed == (arm.name != "bare") and advertised == arm.upscaled


def completed(root: Path) -> bool:
    """Require normal runner exit and its final results, not a partial prefix."""
    marker = root / "exit-code"
    results = root / "piglit"
    return (
        marker.is_file()
        and marker.read_text().strip() == "0"
        and any((results / name).is_file() for name in ("results.json.bz2", "results.json"))
    )


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
    report no regression, having compared a system against itself. Both must
    also have loaded the effect, which a session without OpenGL compositing
    never does.
    """
    routed = "Upscale X11 backend started" in log
    bypassed = "executing stock Xwayland directly" in log
    loaded = any(
        line.startswith("effects: ") and "upscale" in line[len("effects: ") :].split()
        for line in log.splitlines()
    )
    # The transport prints its answer the way Qt prints a variant, so what is
    # looked for is the reason itself rather than a line laid out around it.
    advertised = "connection display advertisement" in log
    return routed and not bypassed and loaded and advertised == arm.upscaled


def read_cases(results: Path) -> dict[str, str]:
    """Read the outcome piglit recorded for every case it completed.

    Partial files are retained for diagnosis, but completed() prevents an
    interrupted arm from qualifying for acceptance.
    """
    packed = results / "results.json.bz2"
    plain = results / "results.json"
    if packed.exists():
        with bz2.open(packed, "rt") as stream:
            return {name: item["result"] for name, item in json.load(stream)["tests"].items()}
    if plain.exists():
        return {
            name: item["result"] for name, item in json.loads(plain.read_text())["tests"].items()
        }
    cases: dict[str, str] = {}
    for path in sorted((results / "tests").glob("*.json")):
        try:
            recorded = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            # The case piglit was writing when it was stopped. One unreadable
            # file is one case missing from both sides of a comparison, which
            # is not worth abandoning the rest of the run for.
            continue
        # One case per file, named at the top level rather than under a key
        # of its own: the assembled file is what gathers them under "tests".
        for name, item in recorded.items():
            if isinstance(item, dict) and "result" in item:
                cases[name] = item["result"]
    return cases


def compare(baseline: dict[str, str], upscaled: dict[str, str]) -> Verdict:
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


def report(suite: str, verdict: Verdict, baseline: str = "present", target: str = "scaling") -> int:
    """Say what the pair showed, and fail only on what this release caused."""
    counts = verdict["completed"]
    print(
        f"{suite} {baseline} -> {target}: {counts['baseline']} baseline cases, "
        f"{counts['upscaled']} target cases, {counts['compared']} comparable"
    )
    for name in verdict["missing"][:20]:
        print(f"  only baseline:   {name}")
    for name in verdict["added"][:20]:
        print(f"  only target:      {name}")
    for name in verdict["repaired"][:20]:
        print(f"  repaired:     {name}")
    for name in verdict["regressed"]:
        print(f"  REGRESSED:    {name}")
    if verdict["missing"] or verdict["added"]:
        print("\nthe arms did not complete the same cases; the pair is incomplete")
        return 1
    if verdict["regressed"]:
        print(f"\n{len(verdict['regressed'])} cases pass in {baseline} and fail in {target}")
        return 1
    if not counts["compared"]:
        print("\nno case completed in both arms; nothing was compared")
        return 1
    print("\nno baseline case regressed")
    return 0


def main(argv: list[str] | None = None) -> int:
    """Run the suite's arms and reject regressions or incomplete comparisons."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=sorted(SUITES), default="xts")
    parser.add_argument(
        "--build", default="build/gcc", help="the build whose effect and transport are measured"
    )
    # The screen this release is written for. The effect advertises a reduced
    # display only where Xwayland already offers that size; at 3840 x 2160 its
    # list carries the 1920 x 1080 the setting asks for (measured 2026-09-27,
    # 5745 connections advertised it).
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--minutes", type=int, default=90, help="how long one arm gets")
    parser.add_argument("--out", default="", help="where the arms are kept")
    arguments = parser.parse_args(argv)
    build = Path(arguments.build).resolve()
    if not (build / "bin" / "Xwayland").exists():
        print(f"no transport at {build}/bin/Xwayland")
        return 1
    if not BASELINE_TREE.exists() or not UPSCALED_TREE.exists():
        print("the suites are missing; run this in the conformance image")
        return 1
    root = (
        Path(arguments.out).resolve()
        if arguments.out
        else Path(tempfile.mkdtemp(prefix="conformance-", dir=build))
    )
    root.mkdir(parents=True, exist_ok=True)
    size = (arguments.width, arguments.height)
    run = Run(build=build, suite=arguments.suite, size=size, minutes=arguments.minutes)
    arms = (Arm("bare", BASELINE_TREE, upscaled=False), *ARMS) if run.suite == "xts" else ARMS
    if run.suite == "xts" and not (build / "bin" / "upscale_x11proxy_conformance").is_file():
        print("missing XTS session driver; build with BUILD_TESTING=ON")
        return 1
    produced = {arm.name: run_arm(arm, root, run) for arm in arms}
    (root / "arms.json").write_text(json.dumps(produced, indent=2))
    # An arm that did not do its own job cannot be compared against the other.
    # Reporting that as "no regression" would be the most expensive kind of
    # green: a gate that passes because it measured nothing.
    idle = [name for name, arm in produced.items() if not arm["engaged"] or not arm["complete"]]
    if idle:
        for name in idle:
            print(f"{name}: did not complete as itself; see {produced[name]['root']}/session.log")
        print("\nthe pair was not comparable, so nothing was measured")
        return 1
    pairs = [("present", "scaling")]
    if run.suite == "xts":
        pairs = [("bare", "present"), ("bare", "scaling"), *pairs]
    verdicts = {}
    outcome = 0
    for baseline, target in pairs:
        verdict = compare(produced[baseline]["cases"], produced[target]["cases"])
        verdicts[f"{baseline}-to-{target}"] = verdict
        outcome = max(outcome, report(arguments.suite, verdict, baseline, target))
    (root / "verdict.json").write_text(json.dumps(verdicts, indent=2))
    print(f"arms under {root}")
    return outcome


if __name__ == "__main__":
    sys.exit(main())
