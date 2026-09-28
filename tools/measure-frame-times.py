#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure what reducing the rendering resolution is worth, on a real session.

The instrument is the effect's own frame statistics. It already counts the
frames the screen presented and the buffers the client committed, keeps the
slow tail of both, and reports them in the text that
``org.kde.kwin.Effects.supportInformation`` returns. Nothing is pushed per
frame: the effect accumulates into a ring buffer and answers when asked, so a
run costs one D-Bus round trip every few seconds rather than one per frame.

The effect follows the screen's frames whether or not anything is drawn on it,
so nothing here switches the on-screen display on. That matters: an overlay is
composited content and holds the output in composition, so measuring with it
shown would charge every run for the instrument. The settings this script does
change are the effect's own, and only for the run.

The script never decides whether a number is good. It reports what it read,
says how many samples it rests on and how far they spread, and leaves runs that
differ by less than that spread to be read as inconclusive.
"""

from __future__ import annotations

import argparse
import contextlib
import csv
import math
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import TextIO

from effect_control import (
    PRESETS,
    RATIOS,
    configure,
    qdbus,
    run_command,
    status,
)
from frame_metrics import Sample, Summary, parse_status, summarize
from measurement_report import (
    announce,
    compare,
    read_environment,
    report,
    write_json,
    write_markdown,
)


def launch(plan: Plan, seconds: int, log: TextIO) -> subprocess.Popen[str]:
    """Start the supplied argument vector, substituting only its duration token."""
    arguments = [item.replace("{seconds}", str(seconds)) for item in plan.command]
    return subprocess.Popen(
        arguments,
        stdout=log,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )


def wait_for_window(window: str, seconds: float) -> bool:
    """Wait for the game's window to exist, within a bounded time.

    A fixed delay is a guess about how long a game takes to start. Sending keys
    before the window exists walks nothing into a scene, and the run then
    measures a game sitting in its menu.
    """
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if run_command(["xdotool", "search", "--classname", window]).stdout.strip():
            return True
        time.sleep(0.5)
    return False


def focus_window(window: str) -> str:
    """Put the keyboard focus on the game's window, or say why it did not.

    Returns what went wrong, or nothing when the window holds the focus.
    """
    found = run_command(["xdotool", "search", "--classname", window])
    identifiers = found.stdout.split()
    if not identifiers:
        return f"no {window} window to type into, so the game stayed in its menu"
    run_command(["xdotool", "windowactivate", "--sync", identifiers[0]])
    # XTEST types into whatever holds the input focus, so an activation that
    # quietly failed would send a menu sequence into whatever the person was
    # last using. The activation's own exit status cannot answer for that
    # here: on a Wayland session xdotool reports that _NET_ACTIVE_WINDOW
    # failed and exits non-zero while having activated the window perfectly
    # well, because that property belongs to an X11 window manager and nothing
    # maintains it. getwindowfocus asks the X server where it will actually
    # send key events, and its answer is compared by window id rather than by
    # name: a class name need not equal the instance name the search matched.
    focused = run_command(["xdotool", "getwindowfocus"]).stdout.strip()
    if focused not in identifiers:
        return (
            f"{window} did not take the keyboard focus, so no keys were sent; "
            f"the focus was on window {focused or 'nothing could name'}"
        )
    return ""


def send_keys(plan: Plan) -> str:
    """Walk a menu-driven game into a running scene, where it needs one.

    Returns what went wrong, or nothing when the keys were sent.
    """
    if not plan.keys:
        return ""
    if not shutil.which("xdotool"):
        return "xdotool is not installed, so the game was left in its menu"
    window = plan.window
    if not wait_for_window(window, seconds=30):
        return f"no {window} window appeared, so no keys were sent and the game stayed in its menu"
    # XTEST delivers ordinary input; XSendEvent can be ignored by toolkits.
    # Check focus before sending keys so they cannot reach another window.
    failure = focus_window(window)
    if failure:
        return failure
    for key in plan.keys:
        # Long enough for a screen to appear, short enough that nobody watches
        # a menu for ten seconds before a run begins.
        time.sleep(0.6)
        sent = run_command(["xdotool", "key", "--delay", "120", key])
        if sent.returncode != 0:
            # The window closed, or the key never arrived. Either way the game
            # is not where the run needs it, and measuring the menu would look
            # like measuring the game.
            return f"could not send {key} to {window}: {sent.stderr.strip() or 'no reason given'}"
    return ""


def stop(process: subprocess.Popen[str]) -> None:
    """End the process group while its output remains in the run log."""
    if process.poll() is None:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
    try:
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
        process.wait(timeout=30)


@dataclass
class Plan:
    """How one run is conducted, so that a run is described rather than listed."""

    game: str = ""
    command: tuple[str, ...] = ()
    window: str = ""
    startup: float = 10.0
    keys: tuple[str, ...] = ()
    rate_pattern: str = ""
    renderer_pattern: str = ""
    logs: Path = Path("build/measurements")
    seconds: float = 60
    interval: float = 2.0
    warm_up: float = 10.0
    sharpening: bool = False
    # Expectations only; the supplied command selects the protocol and renderer.
    window_system: str = ""
    renderer: str = ""
    # Which screen the run is about, so a session with several is unambiguous.
    output: str = ""


def measure(
    plan: Plan, preset: str, run_id: str = "", repeat: int = 1
) -> tuple[Summary, list[Sample]]:
    """Conduct one run: set the preset, start the game, read the instrument, stop."""
    game, seconds, interval, warm_up = plan.game, plan.seconds, plan.interval, plan.warm_up
    sharpening = plan.sharpening
    tool = qdbus()
    misconfigured = configure(preset, sharpening=sharpening)
    if misconfigured:
        # Measuring now would produce a number for the preset before this one,
        # which is worse than producing nothing.
        summary = Summary(game=game, preset=preset, run_id=run_id, repeat=repeat)
        summary.notes.append(f"not measured: {misconfigured}")
        return summary, []
    # Keep stdout in a file: a full pipe would stall the client being measured.
    plan.logs.mkdir(parents=True, exist_ok=True)
    log_path = plan.logs / f"{run_id or 'run'}.log"
    samples: list[Sample] = []
    with log_path.open("w") as log:
        process = launch(plan, int(plan.startup + warm_up + seconds + 15), log)
        try:
            time.sleep(plan.startup)
            driven = send_keys(plan)
            # Shaders compile and caches fill on the first frames of a scene, and
            # they do it again at a resolution the game has not drawn before. A run
            # that counted them would charge the change of resolution for work that
            # happens once.
            time.sleep(warm_up)
            started = time.monotonic()
            # A game that never reached its scene is sitting in a menu, and a menu
            # renders whatever it likes at whatever rate it likes. Sampling it
            # produces figures that look like a measurement and describe nothing,
            # which is how three runs tonight reported a main menu as a benchmark.
            while not driven and time.monotonic() - started < seconds:
                sample = parse_status(status(tool))
                sample.elapsed = round(time.monotonic() - started, 1)
                sample.run_id = run_id
                samples.append(sample)
                if process.poll() is not None:
                    break
                time.sleep(interval)
        finally:
            stop(process)
    summary = summarize(game, preset, samples, plan.window)
    # Readings taken while the effect described some other window are not this
    # game's frames. Without this a run reports the desktop.
    if not any(sample.selected for sample in samples):
        summary.notes.append(
            "the effect never selected this game's window, so nothing here describes the game"
        )
    record_conditions(
        summary,
        plan,
        Conducted(run_id, repeat, driven, log_path.read_text(errors="replace")),
    )
    return summary, samples


@dataclass
class Conducted:
    """What happened while one run was conducted, for its record."""

    run_id: str = ""
    repeat: int = 1
    driven: str = ""
    spoken_output: str = ""


def record_conditions(summary: Summary, plan: Plan, done: Conducted) -> None:
    """Put the run's identity and what actually happened onto its summary.

    Kept apart from conducting the run so that neither is read through the
    other: one starts a game and waits, this one writes down what that was.
    """
    summary.settings = (
        "Application settings are supplied by the operator; not modified or verified."
    )
    summary.command = list(plan.command)
    summary.window = plan.window
    summary.rate_pattern = plan.rate_pattern
    summary.renderer_pattern = plan.renderer_pattern
    summary.run_id = done.run_id
    summary.repeat = done.repeat
    summary.requested_window_system = plan.window_system
    summary.requested_renderer = plan.renderer
    summary.sharpening = plan.sharpening
    summary.seconds = plan.seconds
    summary.warm_up = plan.warm_up
    summary.sampled_every = plan.interval
    if done.driven:
        summary.notes.append(done.driven)
    rate = reported_value(done.spoken_output, plan.rate_pattern)
    if rate:
        try:
            value = float(rate)
            if math.isfinite(value) and value >= 0:
                summary.game_rate = value
            else:
                summary.notes.append("the supplied rate pattern produced an invalid rate")
        except ValueError:
            summary.notes.append("the supplied rate pattern did not produce a number")
    summary.renderer_used = reported_value(done.spoken_output, plan.renderer_pattern)
    if (
        plan.renderer
        and summary.renderer_used
        and plan.renderer.casefold() not in summary.renderer_used.casefold()
    ):
        summary.notes.append(
            f"expected renderer {plan.renderer!r}, observed {summary.renderer_used!r}"
        )
    if plan.window_system and summary.window_system and summary.window_system != plan.window_system:
        summary.notes.append(
            f"asked for the {plan.window_system} window system but the effect saw "
            f"{summary.window_system}; this run did not measure {plan.window_system}"
        )


def reported_value(output: str, pattern: str) -> str:
    """Read one explicitly supplied capture pattern, or leave the value unknown."""
    found = re.search(pattern, output, re.MULTILINE) if pattern else None
    return (found.group(1) or "").strip() if found else ""


def capture_pattern(text: str) -> str:
    """Reject invalid output patterns before a run changes any settings."""
    try:
        pattern = re.compile(text)
    except re.error as problem:
        raise argparse.ArgumentTypeError(str(problem)) from problem
    if pattern.groups != 1:
        message = "use exactly one capture group for the reported value"
        raise argparse.ArgumentTypeError(message)
    return text


# What a reading needs beside it to stand on its own: which run it belongs to
# and how that run was conducted. A row without them can only be understood
# from a report it might get separated from.
PLAN_COLUMNS = (
    "preset",
    "repeat",
    "requested_window_system",
    "requested_renderer",
    "asked_for",
    "sharpening",
    "seconds",
    "warm_up",
    "sampled_every",
)


def write_samples(path: Path, rows: list[tuple[Summary, Sample]]) -> None:
    """Keep every reading, so a summary can be checked rather than believed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        # Every row carries the run it belongs to and the plan that run was
        # conducted under, so rows from repeated runs stay distinguishable and
        # a file of them can be read without the report beside it.
        writer.writerow([*PLAN_COLUMNS, *asdict(Sample()).keys()])
        for summary, sample in rows:
            plan = [getattr(summary, column) for column in PLAN_COLUMNS]
            writer.writerow([*plan, *asdict(sample).values()])


def preset_size(preset: str, output_pixels: str) -> str:
    """Name in pixels what a preset asks the game to render.

    Announced before a run so that the size under test is stated rather than
    left to be worked out from the preset's name and the screen's size.
    """
    ratio = RATIOS.get(preset)
    if not ratio or "x" not in output_pixels:
        return preset
    width, _, height = output_pixels.partition("x")
    try:
        return f"{round(int(width) * ratio)}x{round(int(height) * ratio)}"
    except ValueError:
        return preset


def waiting_seconds(text: str) -> float:
    """Read a duration that a run can actually wait for.

    argparse's float accepts "-1" and "nan", which reach time.sleep() and end
    the run with an exception instead of a message about the command line.
    """
    try:
        value = float(text)
    except ValueError as problem:
        unreadable = f"{text!r} is not a number of seconds"
        raise argparse.ArgumentTypeError(unreadable) from problem
    if not math.isfinite(value) or value < 0:
        impossible = f"{text!r} is not a duration a run can wait for"
        raise argparse.ArgumentTypeError(impossible)
    return value


def positive_seconds(text: str) -> float:
    """Read a duration that has to be more than nothing.

    Zero is a duration a run can wait for, but not one it can sample at: the
    loop would start a process per iteration for the whole run and measure the
    machine's ability to start processes.
    """
    value = waiting_seconds(text)
    if value <= 0:
        too_small = f"{text!r} has to be more than zero"
        raise argparse.ArgumentTypeError(too_small)
    return value


def parse_arguments(argv: list[str] | None) -> tuple[argparse.Namespace, list[str]]:
    """Validate launch and measurement inputs before changing session settings."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--name", default="application", help="label for the report files")
    parser.add_argument("--window", required=True, help="window identity in the effect's metrics")
    parser.add_argument("--startup", type=waiting_seconds, default=10.0)
    parser.add_argument("--key", action="append", default=[], help="X11 key to send after startup")
    parser.add_argument(
        "--rate-pattern", type=capture_pattern, default="", help="stdout regex capturing a rate"
    )
    parser.add_argument(
        "--renderer-pattern",
        type=capture_pattern,
        default="",
        help="stdout regex capturing the renderer",
    )
    parser.add_argument(
        "command",
        nargs=argparse.REMAINDER,
        help="-- program [arguments]; {seconds} expands to run duration",
    )
    parser.add_argument(
        "--presets",
        default="native,quality,performance",
        help="presets to run, in order, starting with the baseline",
    )
    parser.add_argument(
        "--seconds", type=positive_seconds, default=60, help="sampled length of each run"
    )
    parser.add_argument(
        "--interval", type=positive_seconds, default=2.0, help="seconds between readings"
    )
    parser.add_argument(
        "--warm-up", type=waiting_seconds, default=10.0, help="seconds discarded before sampling"
    )
    parser.add_argument("--repeats", type=int, default=1, help="times to run the whole set")
    parser.add_argument("--sharpening", action="store_true", help="run with RCAS on")
    parser.add_argument(
        "--window-system",
        default="",
        choices=["", "wayland", "x11"],
        help="expected window system; select it in the supplied command",
    )
    parser.add_argument(
        "--renderer",
        default="",
        help="expected renderer; select it in the supplied command",
    )
    parser.add_argument("--output", type=Path, default=Path("build/measurements"))
    parser.add_argument(
        "--output-name", default="", help="the screen the run is about, when there is more than one"
    )
    options = parser.parse_args(argv)

    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    if not command or not options.window.strip():
        parser.error("supply a command after -- and a nonempty --window")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", options.name):
        parser.error("--name must contain only letters, digits, underscores and hyphens")
    if options.repeats < 1:
        parser.error("--repeats must be positive")
    presets = [name.strip() for name in options.presets.split(",") if name.strip()]
    unknown = [name for name in presets if name not in PRESETS]
    if not presets:
        parser.error("supply at least one preset")
    if unknown:
        parser.error(f"unknown preset(s): {', '.join(unknown)}")
    options.command = command
    return options, presets


def main(argv: list[str] | None = None) -> int:
    """Run the requested comparisons and report their observations."""
    options, presets = parse_arguments(argv)
    if not os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("DISPLAY"):
        print("no session to measure; this runs on a real desktop, not in a container")
        return 1

    summaries: list[Summary] = []
    rows: list[tuple[Summary, Sample]] = []
    plan = Plan(
        game=options.name,
        command=tuple(options.command),
        window=options.window,
        startup=options.startup,
        keys=tuple(options.key),
        rate_pattern=options.rate_pattern,
        renderer_pattern=options.renderer_pattern,
        logs=options.output,
        seconds=options.seconds,
        interval=options.interval,
        warm_up=options.warm_up,
        sharpening=options.sharpening,
        window_system=options.window_system,
        renderer=options.renderer,
        output=options.output_name,
    )
    stamp = time.strftime("%Y%m%d-%H%M%S")
    conditions = read_environment(qdbus(), options.output_name)
    print("Measuring with:")
    for key, value in asdict(conditions).items():
        print(f"  {key:<20} {value}")

    total = options.repeats * len(presets)
    number = 0
    records: list[dict[str, object]] = []
    for repeat in range(options.repeats):
        # Alternate nothing: run the presets in the order given, repeatedly, so
        # that drift over the session shows up as a difference between repeats
        # rather than hiding inside one of them.
        for preset in presets:
            number += 1
            announce(
                number,
                total,
                {
                    "name": f"{options.name} at {preset}",
                    "command": shlex.join(plan.command),
                    "repeat": f"{repeat + 1} of {options.repeats}",
                    "window API": plan.window_system or "the game chooses",
                    "graphics API": plan.renderer or "the game chooses",
                    "asks the game for": preset_size(preset, conditions.output_pixels),
                    "destination": conditions.output_pixels or "unknown",
                    "sharpening": "on" if plan.sharpening else "off",
                    "sampled for": f"{options.seconds} s after {options.warm_up} s warm-up",
                },
            )
            run_id = f"{options.name}-{stamp}-{repeat + 1:02d}-{preset}"
            summary, samples = measure(plan, preset, run_id, repeat + 1)
            summary.asked_for = preset_size(preset, conditions.output_pixels)
            summaries.append(summary)
            rows.extend((summary, sample) for sample in samples)
            report(summary)
            records.append(asdict(summary))

    stem = options.output / f"{options.name}-{stamp}"
    write_samples(stem.with_suffix(".csv"), rows)
    write_json(stem.with_suffix(".json"), conditions, records)
    write_markdown(stem.with_suffix(".md"), conditions, records)
    compare(summaries)
    print(f"\nreadings  {stem.with_suffix('.csv')}")
    print(f"record    {stem.with_suffix('.json')}")
    print(f"report    {stem.with_suffix('.md')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
