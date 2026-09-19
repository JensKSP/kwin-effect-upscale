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
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from effect_control import (
    PRESETS,
    RATIOS,
    configure,
    qdbus,
    reset_game_resolution,
    run_command,
    status,
)
from frame_metrics import Sample, Summary, parse_status, summarize
from game_output import clear_game_log, game_log, game_reported_rate, game_reported_renderer
from game_settings import Outcome, prepare, still_holds
from measurement_report import announce, read_environment, report, write_json, write_markdown

if TYPE_CHECKING:
    # Only ever named in an annotation, and this module postpones those.
    from collections.abc import Mapping


# The effect's own settings, in the group its KConfig file names. Preset values
# are the ResolutionPreset enum in src/plugins/upscale/resolution.h, in order.
@dataclass(frozen=True)
class Game:
    """How one game is started so that it renders without a person present.

    A demo or profile mode is what makes a run repeatable: the same scene, the
    same route, the same length, so that two runs differ by the resolution and
    by nothing else.
    """

    program: str
    arguments: tuple[str, ...] = ()
    window: str = ""
    keys: tuple[str, ...] = ()
    startup: float = 10.0
    environment: Mapping[str, str] = field(default_factory=dict)


GAMES = {
    "supertuxkart": Game(
        program="supertuxkart",
        # Drives itself for a fixed time and prints its own frame count when it
        # finishes, which is the game's render throughput rather than what the
        # screen presented. Both are worth having: they answer different
        # questions and disagree whenever frames are dropped or repeated.
        arguments=("--profile-time={seconds}", "--fullscreen"),
        window="supertuxkart",
        # SDL chooses its video driver per launch, and on this session it picks
        # X11 even where Wayland is available. An Xwayland client never binds
        # the compositor's wl_output, so the advertised-mode method cannot reach
        # it and the game renders full size. Measured 2026-09-19: forced to
        # Wayland the same build accepts the advertised mode and commits it.
        environment={"SDL_VIDEODRIVER": "wayland"},
    ),
    "extremetuxracer": Game(
        program="etr",
        # No demo mode of its own. The menu is keyboard driven, so the run is
        # started by sending keys to the window; the sequence follows the menu,
        # which moves between versions. Tux then slides the course unattended.
        window="etr",
        keys=("Return", "Return", "Return"),
    ),
    "left4dead2": Game(
        program="steam",
        # Steam hands the request to the running client, so the launch returns
        # long before the game appears and the wait covers the whole startup.
        arguments=("-applaunch", "550", "-novid", "-console"),
        window="left4dead2",
        startup=90.0,
    ),
}


# The effect answers with a line written for programs: untranslated keys and
# values, one space apart. The prose above it is built with i18n and says the
# same things in the session's language, which is exactly why it is not read
# here -- a harness that parsed it would report "nothing was measured" on any
# machine not running in English.
def launch(plan: Plan, seconds: int) -> subprocess.Popen[str]:
    """Start the game in whatever mode renders without a person at the keyboard.

    @p seconds is how long the game has to keep rendering, which is the whole
    run and not only its sampled part: a demo mode told to last as long as the
    sampling window would stop while the warm-up was still being discarded.
    """
    definition = GAMES[plan.game]
    program = shutil.which(definition.program)
    if not program:
        missing = f"{definition.program} is not installed"
        raise SystemExit(missing)
    arguments = [item.format(seconds=seconds) for item in definition.arguments]
    # A new process group, so that stopping the run stops the game and anything
    # it started rather than leaving a renderer behind holding the screen.
    environment = dict(os.environ)
    environment.update(definition.environment)
    # SDL picks its video driver per launch, so naming one is how a game is
    # made a Wayland client or an X11 client on the same session.
    if plan.window_system:
        environment["SDL_VIDEODRIVER"] = plan.window_system
    if plan.renderer:
        arguments.append(f"--render-driver={plan.renderer}")
    return subprocess.Popen(
        [program, *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
        env=environment,
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


def send_keys(game: str) -> str:
    """Walk a menu-driven game into a running scene, where it needs one.

    Returns what went wrong, or nothing when the keys were sent.
    """
    definition = GAMES[game]
    if not definition.keys:
        return ""
    if not shutil.which("xdotool"):
        return "xdotool is not installed, so the game was left in its menu"
    window = definition.window
    if not wait_for_window(window, seconds=30):
        return f"no {window} window appeared, so no keys were sent and the game stayed in its menu"
    for key in definition.keys:
        time.sleep(1.5)
        sent = run_command(
            ["xdotool", "search", "--classname", window, "key", "--window", "%1", key]
        )
        if sent.returncode != 0:
            # The window closed, or the key never arrived. Either way the game
            # is not where the run needs it, and measuring the menu would look
            # like measuring the game.
            return f"could not send {key} to {window}: {sent.stderr.strip() or 'no reason given'}"
    return ""


def stop(process: subprocess.Popen[str]) -> str:
    """End the run and collect whatever the game said on its way out."""
    if process.poll() is None:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
    try:
        output, _ = process.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(os.getpgid(process.pid), signal.SIGKILL)
        output, _ = process.communicate(timeout=30)
    return output or ""


@dataclass
class Plan:
    """How one run is conducted, so that a run is described rather than listed."""

    game: str = ""
    seconds: int = 60
    interval: float = 2.0
    warm_up: float = 10.0
    sharpening: bool = False
    # Empty leaves the game to choose, which is what a player gets.
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
    # Before anything starts, so the run is not inheriting the last one's size.
    clear_game_log(game)
    settings = prepare(game, plan.output)
    print(f"      settings           {settings.describe()}", flush=True)
    reset = reset_game_resolution(game, plan.output)
    startup = GAMES[game].startup
    # The game outlives the sampling window by the time it spends starting and
    # warming up, and then by a margin: a demo that ends one second early takes
    # the last sample with it and leaves the run one reading short.
    process = launch(plan, int(startup + warm_up + seconds + 15))
    samples: list[Sample] = []
    try:
        time.sleep(startup)
        driven = send_keys(game)
        # Shaders compile and caches fill on the first frames of a scene, and
        # they do it again at a resolution the game has not drawn before. A run
        # that counted them would charge the change of resolution for work that
        # happens once.
        time.sleep(warm_up)
        started = time.monotonic()
        while time.monotonic() - started < seconds:
            sample = parse_status(status(tool))
            sample.elapsed = round(time.monotonic() - started, 1)
            sample.run_id = run_id
            samples.append(sample)
            if process.poll() is not None:
                break
            time.sleep(interval)
    finally:
        output = stop(process)
    summary = summarize(game, preset, samples, GAMES[game].window)
    # Readings taken while the effect described some other window are not this
    # game's frames. Without this a run reports the desktop.
    if not any(sample.selected for sample in samples):
        summary.notes.append(
            "the effect never selected this game's window, so nothing here describes the game"
        )
    record_conditions(
        summary,
        plan,
        Conducted(run_id, repeat, settings, driven, output, reset),
    )
    return summary, samples


@dataclass
class Conducted:
    """What happened while one run was conducted, for its record."""

    run_id: str = ""
    repeat: int = 1
    settings: Outcome = field(default_factory=Outcome)
    driven: str = ""
    spoken_output: str = ""
    reset: str = ""


def record_conditions(summary: Summary, plan: Plan, done: Conducted) -> None:
    """Put the run's identity and what actually happened onto its summary.

    Kept apart from conducting the run so that neither is read through the
    other: one starts a game and waits, this one writes down what that was.
    """
    # What the game says it did, against what it was asked to do. A request is
    # not proof: SDL falls back to another video driver without complaint, and
    # a renderer a build does not carry is simply not the one that ran. A run
    # that measured something other than what it was set up to measure has to
    # say so rather than be read as the case it was named after.
    summary.started_at = done.reset
    summary.settings = done.settings.describe()
    # The application writes its own settings on the way out, so what was
    # verified before the run is not necessarily what the run ended with.
    kept = still_holds(plan.game)
    if not kept.controlled:
        summary.notes.append(f"settings changed while running: {kept.describe()}")
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
    spoken = done.spoken_output + game_log(plan.game)
    summary.game_rate = game_reported_rate(spoken)
    summary.renderer_used = game_reported_renderer(spoken, plan.game)
    if plan.renderer and summary.renderer_used:
        wanted = plan.renderer.replace("gl", "opengl")
        if wanted not in summary.renderer_used.lower().replace(" ", ""):
            summary.notes.append(
                f"asked for the {plan.renderer} renderer but the game reports "
                f"{summary.renderer_used!r}; this run did not measure {plan.renderer}"
            )
    if plan.window_system and summary.window_system and summary.window_system != plan.window_system:
        summary.notes.append(
            f"asked for the {plan.window_system} window system but the effect saw "
            f"{summary.window_system}; this run did not measure {plan.window_system}"
        )


def write_samples(path: Path, rows: list[tuple[str, Sample]]) -> None:
    """Keep every reading, so a summary can be checked rather than believed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        # Every row names the run it belongs to, so rows from repeated runs
        # stay distinguishable once they are together in one file.
        writer.writerow(["preset", *asdict(Sample()).keys()])
        for preset, sample in rows:
            writer.writerow([preset, *asdict(sample).values()])


def figure(value: float | None, digits: int = 1) -> str:
    """Format a number, or the dash that says it was never measured."""
    return "-" if value is None else f"{value:.{digits}f}"


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


def compare(summaries: list[Summary]) -> None:
    """Print the runs beside each other, with the baseline as the reference."""
    print()
    header = f"{'preset':<14}{'supplied':<12}{'presented/s':>12}{'frame ms':>10}"
    print(header + f"{'99th ms':>9}{'1% low/s':>10}{'client/s':>10}{'game/s':>9}{'spread':>8}")
    print(
        "-"
        * len(header + f"{'99th ms':>9}{'1% low/s':>10}{'client/s':>10}{'game/s':>9}{'spread':>8}")
    )
    for summary in summaries:
        print(
            f"{summary.preset:<14}{summary.supplied or '-':<12}"
            f"{figure(summary.presented_rate):>12}{figure(summary.frame_time, 2):>10}"
            f"{figure(summary.presented_percentile):>9}{figure(summary.presented_low):>10}"
            f"{figure(summary.client_updates):>10}{figure(summary.game_rate):>9}"
            f"{figure(summary.presented_spread):>8}"
        )
    baseline = next((item for item in summaries if item.preset == "native"), None)
    if not baseline or not baseline.frame_time:
        return
    print()
    for summary in summaries:
        if summary is baseline or not summary.frame_time:
            continue
        change = summary.frame_time - baseline.frame_time
        percent = 100.0 * change / baseline.frame_time
        # Both in milliseconds. A spread in frames per second compared against
        # a difference in milliseconds is not a comparison, and would call a
        # real change inconclusive or an inconclusive one real depending only
        # on where the rates happened to sit.
        spread = (baseline.frame_time_spread or 0) + (summary.frame_time_spread or 0)
        verdict = "within run-to-run spread" if abs(change) < spread else "outside the spread"
        print(
            f"{summary.preset} vs native: frame time {change:+.2f} ms ({percent:+.1f}%), {verdict}"
        )
    for summary in summaries:
        for note in summary.notes:
            print(f"note ({summary.preset}): {note}")


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


def main(argv: list[str] | None = None) -> int:
    """Run the comparison the handbook's matrix asks for and report it."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game", choices=sorted(GAMES))
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
        help="tell the game which window system to use",
    )
    parser.add_argument(
        "--renderer",
        default="",
        choices=["", "gl", "vulkan"],
        help="tell the game which graphics API to use",
    )
    parser.add_argument("--output", type=Path, default=Path("build/measurements"))
    parser.add_argument(
        "--output-name", default="", help="the screen the run is about, when there is more than one"
    )
    options = parser.parse_args(argv)

    presets = [name.strip() for name in options.presets.split(",") if name.strip()]
    unknown = [name for name in presets if name not in PRESETS]
    if unknown:
        parser.error(f"unknown preset(s): {', '.join(unknown)}")
    if not os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("DISPLAY"):
        parser.error("no session to measure; this runs on a real desktop, not in a container")

    summaries: list[Summary] = []
    rows: list[tuple[str, Sample]] = []
    plan = Plan(
        game=options.game,
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
                    "name": f"{options.game} at {preset}",
                    "repeat": f"{repeat + 1} of {options.repeats}",
                    "window API": plan.window_system or "the game chooses",
                    "graphics API": plan.renderer or "the game chooses",
                    "asks the game for": preset_size(preset, conditions.output_pixels),
                    "destination": conditions.output_pixels or "unknown",
                    "sharpening": "on" if plan.sharpening else "off",
                    "sampled for": f"{options.seconds} s after {options.warm_up} s warm-up",
                },
            )
            run_id = f"{options.game}-{stamp}-{repeat + 1:02d}-{preset}"
            summary, samples = measure(plan, preset, run_id, repeat + 1)
            summary.asked_for = preset_size(preset, conditions.output_pixels)
            summaries.append(summary)
            rows.extend((preset, sample) for sample in samples)
            report(summary)
            records.append(asdict(summary))

    stem = options.output / f"{options.game}-{stamp}"
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
