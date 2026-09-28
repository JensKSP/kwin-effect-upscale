#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare KDE's Wayland integration tests with three production-effect states.

Run inside containers/wayland-tests in the project VM, with its virtual DRM
device. Prepare the upstream source with prepare-kwin-tests.py first. Every
arm creates its own OpenGL test compositor; no outer desktop is involved.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import re
import signal
import subprocess
import tempfile
import xml.etree.ElementTree as ET  # nosec B405 - Only our private QtTest output is parsed.
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path

ARMS = ("absent", "idle", "active")
TIMED_OUT = 124

# Cases the supported scope excludes by name (doc/upscaling.md, "A release is
# as conformant as what it sits on"): each fails with this effect for a reason
# that is not a defect of it. They are reported as excluded, never dropped, and
# only in the arms named here; the same change anywhere else still fails.
LOADED_EFFECTS = "asserts that exactly one effect is loaded, and this one is a second"
SCALE_CONFIGURE = (
    "the effect acts on this window; asking it for another scale, or giving the scale "
    "back, sends a configure of its own, one more than the test counts"
)
LOADED_EFFECTS_CASES = (
    "kwin-testDontCrashReinitializeCompositor/testReinitializeCompositor:Fade",
    "kwin-testToplevelOpenCloseAnimation/testAnimateToplevels:Fade",
    "kwin-testPopupOpenCloseAnimation/testAnimatePopups",
    "kwin-testDesktopSwitchingAnimation/testSwitchDesktops:Fade Desktop",
    "kwin-testMinimizeAnimation/testMinimizeUnminimize:Magic Lamp",
    "kwin-testMaximizeAnimation/testMaximizeRestore",
)
SCALE_CONFIGURE_CASES = (
    "kwin-testInputMethod/testOpenClose",
    "kwin-testOutputChanges/testMaximizeStateRestoredAfterEnablingOutput:Full Maximization",
    "kwin-testXdgShellWindow/testFullscreen:server-side deco",
    "kwin-testXdgShellWindow/testMaximizedToFullscreen:server-side deco",
    "kwin-testXdgShellWindowRules/testMaximizeApply",
    "kwin-testXdgShellWindowRules/testMaximizeApplyNow",
    "kwin-testXdgShellWindowRules/testMaximizeForce",
    "kwin-testXdgShellWindowRules/testMaximizeForceTemporarily",
    "kwin-testXdgShellWindowRules/testMaximizeRemember",
)
EXCLUDED: dict[str, tuple[tuple[str, ...], str]] = {
    **dict.fromkeys(LOADED_EFFECTS_CASES, (("idle", "active"), LOADED_EFFECTS)),
    **dict.fromkeys(SCALE_CONFIGURE_CASES, (("active",), SCALE_CONFIGURE)),
    "kwin-testScreenChanges/testScreenAddRemove": (
        ("active",),
        "a program the effect acts on is told the reduced output mode when it binds the output",
    ),
}


@dataclass(frozen=True)
class Run:
    """The common binaries, destination and deadline for all comparison arms."""

    output: Path
    effect: Path
    kwin: Path
    timeout: int


@dataclass
class Result:
    """One executable's individual assertions and evidence of its test setup."""

    name: str
    arm: str
    exit_code: int
    engaged: bool
    scaled: bool
    complete: bool
    cases: dict[str, str]
    effect_lost: bool = False


def read_cases(path: Path) -> tuple[dict[str, str], bool]:
    """Keep skips, expected failures and data rows distinct from passes."""
    try:
        # This is produced by our own QtTest process in its private directory,
        # never downloaded XML or client-supplied protocol data.
        root = ET.parse(path).getroot()  # noqa: S314  # nosec B314
    except (OSError, ET.ParseError):
        return {}, False
    cases: dict[str, str] = {}
    for function in root.findall("TestFunction"):
        name = function.attrib["name"]
        for incident in function:
            if incident.tag != "Incident" and not (
                incident.tag == "Message" and incident.get("type") == "skip"
            ):
                continue
            tag = incident.findtext("DataTag", "")
            key = f"{name}:{tag}" if tag else name
            outcome = incident.attrib["type"]
            # A row can log more than one failure; never overwrite it with a
            # later success message from the same function.
            if cases.get(key) not in {"fail", "xpass"}:
                cases[key] = outcome
    complete = "cleanupTestCase" in cases and bool(cases)
    return cases, complete


def discover(build: Path, pattern: str) -> list[tuple[str, list[str]]]:
    """Use upstream's registered tests, including its subdirectories."""
    output = subprocess.check_output(
        ["ctest", "--test-dir", str(build / "autotests/integration"), "--show-only=json-v1"],
        text=True,
    )
    selected = [test for test in json.loads(output)["tests"] if re.search(pattern, test["name"])]
    missing = [str(test["name"]) for test in selected if "command" not in test]
    if missing:
        message = "upstream tests have not been built: " + ", ".join(missing)
        raise RuntimeError(message)
    return [(str(test["name"]), [str(value) for value in test["command"]]) for test in selected]


def run_test(name: str, command: list[str], arm: str, run: Run) -> Result:
    """Run one test on its private bus and terminate all children on timeout."""
    cell = run.output / arm / name
    cell.mkdir(parents=True)
    xml = cell / "result.xml"
    log_path = cell / "session.log"
    project = Path(__file__).resolve().parent.parent
    # Upstream's socket names are long. Keep their private runtime path short
    # enough for Unix sockets while retaining all generated files under build/.
    with tempfile.TemporaryDirectory(prefix="wc-", dir=project / "build") as directory:
        runtime = Path(directory)
        for child in ("home", "config", "data", "cache", "empty"):
            (runtime / child).mkdir()
        environment = dict(os.environ)
        environment.update(
            HOME=str(runtime / "home"),
            XDG_RUNTIME_DIR=str(runtime),
            XDG_CONFIG_HOME=str(runtime / "config"),
            XDG_DATA_HOME=str(runtime / "data"),
            XDG_CACHE_HOME=str(runtime / "cache"),
            XDG_CONFIG_DIRS=str(runtime / "empty"),
            UPSCALE_CONFORMANCE_ARM=arm,
            UPSCALE_CONFORMANCE_IMAGES=str(cell),
            LIBGL_ALWAYS_SOFTWARE="1",
            GBM_ALWAYS_SOFTWARE="1",
            LP_NUM_THREADS="2",
            QT_PLUGIN_PATH=f"{run.effect / 'bin'}:{run.kwin / 'bin'}",
            LC_ALL="C.UTF-8",
            QT_LOGGING_RULES="kwin_effect_upscale=true",
            QT_LOGGING_TO_CONSOLE="1",
        )
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "QT_QPA_PLATFORM"):
            environment.pop(key, None)
        with log_path.open("w") as log:
            process = subprocess.Popen(
                [*command, "-o", f"{xml},xml"],
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            try:
                code = process.wait(timeout=run.timeout)
            except subprocess.TimeoutExpired:
                code = TIMED_OUT
            finally:
                # KWin test crashes can leave helper processes behind even
                # when their main process exited before the deadline.
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        text = log_path.read_text(errors="replace")
        xml_text = xml.read_text(errors="replace") if xml.exists() else ""
        text += xml_text
        marker = f"UPSCALE_CONFORMANCE arm={arm} opengl=1 loaded={int(arm != 'absent')}"
        cases, complete = read_cases(xml)
        result = Result(
            name,
            arm,
            code,
            marker in text,
            "UPSCALE_CONFORMANCE rendered" in text,
            complete,
            cases,
            effect_lost=arm != "absent" and "UPSCALE_CONFORMANCE state loaded=0" in xml_text,
        )
        (cell / "result.json").write_text(json.dumps(asdict(result), indent=2) + "\n")
        return result


def differences(control: Result, result: Result) -> tuple[list[str], list[str]]:
    """Describe changed outcomes without treating missing cases as passes.

    Returns the changes, and those the supported scope excludes by name. An
    executable's exit code counts as excluded only when every case it changed
    is, so that an excluded case cannot hide another behind the same exit.
    """
    changed = []
    excluded = []
    for case in sorted(control.cases.keys() | result.cases.keys()):
        before = control.cases.get(case, "missing")
        after = result.cases.get(case, "missing")
        if before == after:
            continue
        if result.arm == "active" or before == "pass" or "missing" in (before, after):
            line = f"{result.name}/{case}: {before} -> {after}"
            arms, reason = EXCLUDED.get(f"{result.name}/{case}", ((), ""))
            if result.arm in arms and "missing" not in (before, after):
                excluded.append(f"{line} ({reason})")
            else:
                changed.append(line)
    if result.exit_code != control.exit_code:
        line = f"{result.name}/exit: {control.exit_code} -> {result.exit_code}"
        (excluded if excluded and not changed else changed).append(line)
    return changed, excluded


def missing_arms(results: list[Result]) -> list[str]:
    """Require a control and both effect states for every upstream suite."""
    names = {result.name for result in results} - {"kwin-testUpscaleProduction"}
    present = {(result.name, result.arm) for result in results}
    return [
        f"{arm}/{name}: missing arm"
        for name in sorted(names)
        for arm in ARMS
        if (name, arm) not in present
    ]


def exercised(result: Result) -> bool:
    """Require compositor engagement and a complete, non-crashing run."""
    if (
        not result.engaged
        or not result.complete
        or result.exit_code < 0
        or result.exit_code == TIMED_OUT
    ):
        return False
    if result.name == "kwin-testUpscaleProduction":
        return result.scaled and result.exit_code == 0 and set(result.cases.values()) == {"pass"}
    return not result.effect_lost


def compare(results: list[Result]) -> dict[str, list[str]]:
    """Reject missing or unexercised arms rather than calling them clean."""
    verdict: dict[str, list[str]] = {
        "invalid": missing_arms(results),
        "baseline_failures": [],
        "idle_regressions": [],
        "active_differences": [],
        "excluded": [],
        "scaled": [],
    }
    baseline = {result.name: result for result in results if result.arm == "absent"}
    for result in results:
        label = f"{result.arm}/{result.name}"
        if not exercised(result):
            verdict["invalid"].append(label)
        if result.scaled:
            verdict["scaled"].append(label)
        if result.name == "kwin-testUpscaleProduction":
            continue
        if result.arm == "absent":
            if result.exit_code != 0:
                verdict["baseline_failures"].append(label)
            continue
        control = baseline.get(result.name)
        if not control:
            verdict["invalid"].append(f"{label}: missing baseline")
            continue
        key = "idle_regressions" if result.arm == "idle" else "active_differences"
        changed, excluded = differences(control, result)
        verdict[key].extend(changed)
        verdict["excluded"].extend(f"{result.arm}: {line}" for line in excluded)
    # A run the baseline cannot complete either is the system's, as a case that
    # fails both ways is: recorded, and not held against the effect.
    unfinished = {
        label.split("/", 1)[1]
        for label in verdict["invalid"]
        if label.startswith("absent/") and ":" not in label
    }
    verdict["baseline_invalid"] = [
        label
        for label in verdict["invalid"]
        if ":" not in label and label.split("/", 1)[1] in unfinished
    ]
    verdict["invalid"] = [
        label for label in verdict["invalid"] if label not in verdict["baseline_invalid"]
    ]
    return verdict


def main() -> int:
    """Keep every case's results available for investigation and repeat runs."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kwin-build", type=Path, default=Path("build/wayland-conformance/kwin"))
    parser.add_argument(
        "--effect-build", type=Path, default=Path("build/wayland-conformance/effect")
    )
    parser.add_argument("--out", type=Path)
    parser.add_argument("--match", default=".")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--scaling-only", action="store_true")
    parser.add_argument("--jobs", type=int, default=1)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    project = Path(__file__).resolve().parent.parent
    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S%fZ")
    output = (args.out or project / "build/wayland-conformance" / stamp).resolve()
    if not output.is_relative_to(project / "build"):
        parser.error("results must be under this repository's build directory")
    if output.exists():
        parser.error("output already exists; choose a fresh result directory")
    output.mkdir(parents=True)
    print(f"Results: {output}", flush=True)
    tests = discover(
        args.kwin_build.resolve(), "testUpscaleProduction" if args.scaling_only else args.match
    )
    if not tests:
        parser.error("no built upstream integration tests match")
    results: list[Result] = []
    run = Run(output, args.effect_build.resolve(), args.kwin_build.resolve(), args.timeout)
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        pending = [
            executor.submit(run_test, name, command, arm, run)
            for name, command in tests
            for arm in (("active",) if name == "kwin-testUpscaleProduction" else ARMS)
        ]
        for future in as_completed(pending):
            result = future.result()
            results.append(result)
            counts = {
                state: list(result.cases.values()).count(state)
                for state in sorted(set(result.cases.values()))
            }
            print(
                f"{result.name} {result.arm}: exit={result.exit_code} engaged={result.engaged} "
                f"complete={result.complete} scaled={result.scaled} {counts}",
                flush=True,
            )
            (output / "results.json").write_text(
                json.dumps([asdict(item) for item in results], indent=2)
            )
    verdict = compare(results)
    (output / "verdict.json").write_text(json.dumps(verdict, indent=2) + "\n")
    print(json.dumps(verdict, indent=2), flush=True)
    return int(any(verdict[key] for key in ("invalid", "idle_regressions", "active_differences")))


if __name__ == "__main__":
    raise SystemExit(main())
