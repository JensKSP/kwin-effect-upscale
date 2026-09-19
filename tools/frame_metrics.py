# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read what the effect measured, and reduce a run's readings to figures.

Separated from the script that drives the runs because this half is the part
with a contract: the machine line the effect writes, what each key means, and
the rules for refusing a reading rather than reporting it. That contract is
what the tests exercise, and it changes for different reasons than the
business of starting games and setting presets does.
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass, field

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
    "scanout": ("scanout", str),
    "windowsystem": ("window_system", str),
    "buffer": ("buffer_kind", str),
}


@dataclass
class Sample:
    """One reading of the accumulated statistics, with the time it was taken."""

    # Which run this reading belongs to. Rows from repeated runs are otherwise
    # indistinguishable once they are in one file.
    run_id: str = ""
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
    # Whether the output kept direct scanout for this frame. Losing it is part
    # of what enabling the effect costs, so a comparison that did not record it
    # is comparing two different presentation paths without saying so.
    scanout: str = ""
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


def describes(sample: Sample, window: str) -> bool:
    """Whether this reading is about the named window rather than another one.

    Takes the window rather than the game so that this half knows nothing about
    starting games. The effect follows whatever window it can describe, so a
    reading that names another one is the desktop's, not the game's.
    """
    return bool(sample.window) and window.lower() in sample.window.lower()


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

    run_id: str = ""
    game: str = ""
    preset: str = ""
    repeat: int = 1
    requested_window_system: str = ""
    requested_renderer: str = ""
    asked_for: str = ""
    sharpening: bool = False
    seconds: float = 0.0
    warm_up: float = 0.0
    sampled_every: float = 0.0
    samples: int = 0
    supplied: str = ""
    destination: str = ""
    scaling: bool = False
    window_system: str = ""
    buffer_kind: str = ""
    presented_rate: float | None = None
    presented_spread: float | None = None
    # The same spread expressed as frame time, because that is what a
    # difference between runs is stated in. A spread in frames per second
    # cannot be compared against a difference in milliseconds.
    frame_time_spread: float | None = None
    frame_time: float | None = None
    presented_low: float | None = None
    presented_percentile: float | None = None
    presented_worst: float | None = None
    client_updates: float | None = None
    # The spread of that rate across the run's samples, for the same reason
    # the presented rate carries one: two runs closer together than a single
    # run's own readings have not been shown to differ.
    client_spread: float | None = None
    game_rate: float | None = None
    renderer_used: str = ""
    scanout: str = ""
    # The size the game was put back to before it started, so a table can show
    # that a run began from the screen rather than from the run before it.
    started_at: str = ""
    # What controlling the application's own settings achieved, so a reader can
    # see whether the run was conducted under known conditions.
    settings: str = ""
    notes: list[str] = field(default_factory=list)


def median_of(samples: list[Sample], name: str) -> float | None:
    """Take the median of one field, ignoring samples that did not carry it."""
    values = [getattr(sample, name) for sample in samples]
    present = [value for value in values if value is not None]
    return statistics.median(present) if present else None


def summarize(game: str, preset: str, samples: list[Sample], window: str = "") -> Summary:
    """Reduce a run's samples to the figures a comparison is made of.

    The median rather than the mean, because a run interrupted by something
    else on the machine produces one wild sample and no amount of averaging
    hides it. The spread is reported beside it so that two runs closer together
    than their own samples are not read as a difference.
    """
    wanted = window or game
    useful = [sample for sample in samples if measuring(sample) and describes(sample, wanted)]
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
    summary.scanout = last.scanout
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
    committed = [sample.client_updates for sample in useful if sample.client_updates is not None]
    if len(committed) > 1:
        summary.client_spread = max(committed) - min(committed)
    rates = [sample.presented_rate for sample in useful if sample.presented_rate]
    if len(rates) > 1:
        summary.presented_spread = max(rates) - min(rates)
        # Converted rather than reused: the slowest reading gives the longest
        # frame time and the fastest the shortest, so the spread in
        # milliseconds is the difference between those two, not the difference
        # between the rates.
        summary.frame_time_spread = 1000.0 / min(rates) - 1000.0 / max(rates)
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
