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
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    # Only ever named in an annotation, and this module postpones those.
    from collections.abc import Mapping

# The effect's own settings, in the group its KConfig file names. Preset values
# are the ResolutionPreset enum in src/plugins/upscale/resolution.h, in order.
GROUP = "Effect-upscale"
PRESETS = {
    "automatic": 0,
    "native": 1,
    "ultra-quality": 2,
    "quality": 3,
    "balanced": 4,
    "performance": 5,
    "custom": 6,
}

# What a preset asks for, as a fraction of the output. Reported alongside the
# measurement so a table says what was rendered, not only which word was set.
RATIOS = {
    "native": 1.0,
    "ultra-quality": 1.0 / 1.3,
    "quality": 1.0 / 1.5,
    "balanced": 1.0 / 1.7,
    "performance": 0.5,
}


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
METRICS_PREFIX = "metrics:"

# What each key means here, and how to read it. A key the effect did not write
# is a measurement it did not have, and stays None rather than becoming zero.
METRIC_FIELDS = {
    "presented": ("presented_rate", float),
    "low": ("presented_low", float),
    "p99": ("presented_percentile", float),
    "worst": ("presented_worst", float),
    "frames": ("presented_frames", int),
    "client": ("client_updates", float),
    "repaints": ("repaints", float),
    "interval": ("interval", float),
    "supplied": ("supplied", str),
    "destination": ("destination", str),
    "scaling": ("scaling", bool),
    "selected": ("selected", bool),
    "window": ("window", str),
    "windowsystem": ("window_system", str),
    "buffer": ("buffer_kind", str),
}


@dataclass
class Sample:
    """One reading of the accumulated statistics, with the time it was taken."""

    elapsed: float = 0.0
    presented_rate: float | None = None
    presented_low: float | None = None
    presented_percentile: float | None = None
    presented_worst: float | None = None
    presented_frames: int | None = None
    client_updates: float | None = None
    repaints: float | None = None
    interval: float | None = None
    supplied: str = ""
    destination: str = ""
    scaling: bool = False
    selected: bool = False
    window: str = ""
    window_system: str = ""
    buffer_kind: str = ""


def parse_status(text: str) -> Sample:
    """Read one support-information answer into a sample.

    Only the machine line is read. Everything above it is translated, and a
    comparison that depended on the session's language would report nothing
    measured on most of the machines this effect runs on.
    """
    sample = Sample()
    line = next((one for one in text.splitlines() if one.startswith(METRICS_PREFIX)), None)
    if line is None:
        return sample
    for entry in line[len(METRICS_PREFIX) :].split():
        key, separator, value = entry.partition("=")
        if not separator or key not in METRIC_FIELDS:
            continue
        name, kind = METRIC_FIELDS[key]
        try:
            setattr(
                sample, name, value if kind is str else kind(int(value) if kind is bool else value)
            )
        except ValueError:
            # A value this build writes differently is skipped rather than
            # guessed at; the summary then reports it as not measured.
            continue
    return sample


def describes(sample: Sample, game: str) -> bool:
    """Whether this reading is about the game rather than some other window."""
    expected = GAMES[game].window.lower()
    return bool(sample.window) and expected in sample.window.lower()


def measuring(sample: Sample) -> bool:
    """Whether a sample carries a presented rate worth keeping.

    A reading taken before the first sampling interval completed has no rate at
    all, and one taken from a stopped game repeats the last one it had. Both
    would drag an average towards a number the run never ran at.
    """
    return sample.presented_rate is not None and sample.presented_rate > 0


@dataclass
class Summary:
    """What a run came to, across the samples that were actually measuring."""

    game: str = ""
    preset: str = ""
    samples: int = 0
    supplied: str = ""
    destination: str = ""
    scaling: bool = False
    window_system: str = ""
    buffer_kind: str = ""
    presented_rate: float | None = None
    presented_spread: float | None = None
    frame_time: float | None = None
    presented_low: float | None = None
    presented_percentile: float | None = None
    presented_worst: float | None = None
    client_updates: float | None = None
    game_rate: float | None = None
    renderer_used: str = ""
    notes: list[str] = field(default_factory=list)


def median_of(samples: list[Sample], name: str) -> float | None:
    """Take the median of one field, ignoring samples that did not carry it."""
    values = [getattr(sample, name) for sample in samples]
    present = [value for value in values if value is not None]
    return statistics.median(present) if present else None


def summarize(game: str, preset: str, samples: list[Sample]) -> Summary:
    """Reduce a run's samples to the figures a comparison is made of.

    The median rather than the mean, because a run interrupted by something
    else on the machine produces one wild sample and no amount of averaging
    hides it. The spread is reported beside it so that two runs closer together
    than their own samples are not read as a difference.
    """
    useful = [sample for sample in samples if measuring(sample) and describes(sample, game)]
    summary = Summary(game=game, preset=preset, samples=len(useful))
    if not useful:
        summary.notes.append(
            "nothing was measured for this game's window; it never rendered, or the "
            "effect was following another window for the whole run"
        )
        return summary
    last = useful[-1]
    summary.supplied = last.supplied
    summary.destination = last.destination
    summary.scaling = any(sample.scaling for sample in useful)
    summary.window_system = last.window_system
    summary.buffer_kind = last.buffer_kind
    # A run that reduced the buffer but was never scaled is the failure worth
    # naming: the game did what was asked and the effect still handed the frame
    # back. The reason is in the effect's own display, which is translated.
    if (
        summary.supplied
        and summary.destination
        and summary.supplied != summary.destination
        and not summary.scaling
    ):
        summary.notes.append(
            f"supplied {summary.supplied} for {summary.destination} but nothing was scaled; "
            "the effect's display gives the reason"
        )
    summary.presented_rate = median_of(useful, "presented_rate")
    summary.presented_low = median_of(useful, "presented_low")
    summary.presented_percentile = median_of(useful, "presented_percentile")
    summary.presented_worst = median_of(useful, "presented_worst")
    summary.client_updates = median_of(useful, "client_updates")
    rates = [sample.presented_rate for sample in useful if sample.presented_rate]
    if len(rates) > 1:
        summary.presented_spread = max(rates) - min(rates)
    if summary.presented_rate:
        # The frame time the presented rate implies, which is what it is: the
        # reciprocal of an average, not a measured mean frame time. The
        # percentile beside it is measured, and is the one to quote for a tail.
        summary.frame_time = 1000.0 / summary.presented_rate
    # A game drawing far more frames than the screen showed is not being
    # measured by the presented rate: that rate is the screen's refresh, and
    # two runs that both reach it say nothing about their rendering cost. Say
    # so on the run rather than leaving a reader to compare two refresh rates
    # and conclude the resolution made no difference.
    if (
        summary.client_updates
        and summary.presented_rate
        and summary.client_updates > summary.presented_rate * 1.2
    ):
        summary.notes.append(
            f"presented rate is limited by the screen ({summary.presented_rate:.0f}/s) "
            f"while the game drew {summary.client_updates:.0f}/s; "
            "compare client buffer updates, not presented"
        )
    return summary


def run_command(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    """Run a helper and return it, without raising on a non-zero exit."""
    return subprocess.run(arguments, capture_output=True, text=True, check=False)


def qdbus() -> str:
    """Find the Qt D-Bus helper this session has, under either of its names."""
    for name in ("qdbus6", "qdbus-qt6", "qdbus"):
        found = shutil.which(name)
        if found:
            return found
    missing = "no qdbus binary found; install qt6-tools or qttools5-dev-tools"
    raise SystemExit(missing)


def status(tool: str) -> str:
    """Ask the running effect for its current state, accumulated."""
    result = run_command(
        [tool, "org.kde.KWin", "/Effects", "org.kde.kwin.Effects.supportInformation", "upscale"]
    )
    return result.stdout


def configure(preset: str, *, sharpening: bool) -> None:
    """Set the effect's own settings for the next run and apply them.

    These are the effect's own settings, not the session's: the preset under
    test, resolution control and the sharpening state. The on-screen display is
    left exactly as the user had it, because measuring does not depend on it
    and showing it would cost every run the same composition it saves.
    """
    settings = {
        "Preset": str(PRESETS[preset]),
        "ResolutionControl": "true",
        "Sharpening": "true" if sharpening else "false",
    }
    for key, value in settings.items():
        run_command(["kwriteconfig6", "--file", "kwinrc", "--group", GROUP, "--key", key, value])
    run_command(
        [qdbus(), "org.kde.KWin", "/Effects", "org.kde.kwin.Effects.reconfigureEffect", "upscale"]
    )


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


def send_keys(game: str) -> None:
    """Walk a menu-driven game into a running scene, where it needs one."""
    definition = GAMES[game]
    if not definition.keys or not shutil.which("xdotool"):
        return
    window = definition.window
    for key in definition.keys:
        time.sleep(1.5)
        run_command(["xdotool", "search", "--classname", window, "key", "--window", "%1", key])


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


def game_reported_rate(output: str) -> float | None:
    """Read the frame rate a game printed itself, where its demo mode prints one."""
    # SuperTuxKart's profile mode ends with a summary naming the frames it drew
    # and the time it took. That is its render throughput, which is not what
    # the screen presented and is reported separately for exactly that reason.
    found = re.search(r"Number of frames:\s*([0-9]+)\s*time\s*([0-9.]+)", output)
    if found and float(found.group(2)) > 0:
        return float(found.group(1)) / float(found.group(2))
    found = re.search(r"FPS\s*[:=]\s*([0-9.]+)", output)
    return float(found.group(1)) if found else None


def game_reported_renderer(output: str, game: str) -> str:
    """Read the graphics API the game says it used, not the one it was asked for.

    A compositor cannot observe this: neither protocol carries a client's
    graphics API and an OpenGL and a Vulkan client hand over the same kind of
    buffer. The game knows, and says so in its own output, so that is where a
    run finds out what it actually measured.
    """
    lines = output.splitlines()
    if game == "supertuxkart":
        # "Using renderer: OpenGL 4.3.0", or Vulkan where that renderer ran.
        found = next((line for line in lines if "Using renderer:" in line), "")
        _, _, named = found.partition("Using renderer:")
        return named.strip()
    found = next((line for line in lines if "renderer" in line.lower()), "")
    return found.strip()


def game_log(game: str) -> str:
    """Whatever the game wrote about itself, where it writes to its own file.

    SuperTuxKart reopens its output onto a log of its own within a second of
    starting, so almost nothing reaches the pipe this script holds.
    """
    if game == "supertuxkart":
        path = Path.home() / ".config/supertuxkart/config-0.10/stdout.log"
        if path.exists():
            return path.read_text(errors="replace")
    return ""


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


def measure(plan: Plan, preset: str) -> tuple[Summary, list[Sample]]:
    """Conduct one run: set the preset, start the game, read the instrument, stop."""
    game, seconds, interval, warm_up = plan.game, plan.seconds, plan.interval, plan.warm_up
    sharpening = plan.sharpening
    tool = qdbus()
    configure(preset, sharpening=sharpening)
    startup = GAMES[game].startup
    # The game outlives the sampling window by the time it spends starting and
    # warming up, and then by a margin: a demo that ends one second early takes
    # the last sample with it and leaves the run one reading short.
    process = launch(plan, int(startup + warm_up + seconds + 15))
    samples: list[Sample] = []
    try:
        time.sleep(startup)
        send_keys(game)
        # Shaders compile and caches fill on the first frames of a scene, and
        # they do it again at a resolution the game has not drawn before. A run
        # that counted them would charge the change of resolution for work that
        # happens once.
        time.sleep(warm_up)
        started = time.monotonic()
        while time.monotonic() - started < seconds:
            sample = parse_status(status(tool))
            sample.elapsed = round(time.monotonic() - started, 1)
            samples.append(sample)
            if process.poll() is not None:
                break
            time.sleep(interval)
    finally:
        output = stop(process)
    summary = summarize(game, preset, samples)
    # What the game says it did, against what it was asked to do. A request is
    # not proof: SDL falls back to another video driver without complaint, and
    # a renderer a build does not carry is simply not the one that ran. A run
    # that measured something other than what it was set up to measure has to
    # say so rather than be read as the case it was named after.
    spoken = output + game_log(plan.game)
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
    # Samples taken while the effect was describing some other window are not
    # this game's frames. Without this a run reports the desktop.
    if not any(sample.selected for sample in samples):
        summary.notes.append(
            "the effect never selected this game's window, so nothing here describes the game"
        )
    return summary, samples


def write_samples(path: Path, rows: list[tuple[str, Sample]]) -> None:
    """Keep every reading, so a summary can be checked rather than believed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["preset", *asdict(Sample()).keys()])
        for preset, sample in rows:
            writer.writerow([preset, *asdict(sample).values()])


def figure(value: float | None, digits: int = 1) -> str:
    """Format a number, or the dash that says it was never measured."""
    return "-" if value is None else f"{value:.{digits}f}"


def report(summaries: list[Summary]) -> None:
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
        spread = (baseline.presented_spread or 0) + (summary.presented_spread or 0)
        verdict = "within run-to-run spread" if abs(change) < spread else "outside the spread"
        print(
            f"{summary.preset} vs native: frame time {change:+.2f} ms ({percent:+.1f}%), {verdict}"
        )
    for summary in summaries:
        for note in summary.notes:
            print(f"note ({summary.preset}): {note}")


def main(argv: list[str] | None = None) -> int:
    """Run the comparison the handbook's matrix asks for and report it."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game", choices=sorted(GAMES))
    parser.add_argument(
        "--presets",
        default="native,quality,performance",
        help="presets to run, in order, starting with the baseline",
    )
    parser.add_argument("--seconds", type=int, default=60, help="sampled length of each run")
    parser.add_argument("--interval", type=float, default=2.0, help="seconds between readings")
    parser.add_argument(
        "--warm-up", type=float, default=10.0, help="seconds discarded before sampling"
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
    )
    for repeat in range(options.repeats):
        # Alternate nothing: run the presets in the order given, repeatedly, so
        # that drift over the session shows up as a difference between repeats
        # rather than hiding inside one of them.
        for preset in presets:
            print(f"run {repeat + 1}/{options.repeats}: {options.game} at {preset}", flush=True)
            summary, samples = measure(plan, preset)
            summaries.append(summary)
            rows.extend((preset, sample) for sample in samples)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = options.output / f"{options.game}-{stamp}.csv"
    write_samples(path, rows)
    report(summaries)
    print(f"\nsamples written to {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
