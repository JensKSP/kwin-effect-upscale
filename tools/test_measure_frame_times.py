# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for reading the effect's measurements out of its status."""

import runpy
import sys
import unittest
from pathlib import Path

# The script imports its sibling the way every tool here does, which works
# because Python puts a script's own directory on the path. Loading it by path
# does not, so the directory is named before it is loaded.
sys.path.insert(0, str(Path(__file__).parent))

HARNESS = runpy.run_path(str(Path(__file__).with_name("measure-frame-times.py")))
game_reported_rate = HARNESS["game_reported_rate"]
game_reported_renderer = HARNESS["game_reported_renderer"]

from frame_metrics import Sample, parse_status, summarize  # noqa: E402

# A status as the effect answers it while it is scaling and measuring.
#
# The prose carries figures that disagree with the machine line on purpose. In
# a session running in another language that prose is translated and the
# machine line is not, so a parser must take the machine line and nothing else.
# Disagreeing numbers prove that here without writing the fixture in a language
# this project does not otherwise use: a parser that read the prose would
# report 1.0 and 99 frames, which no assertion below accepts.
SCALING = """Desired: 2560 x 1440 requested from SuperTuxKart as its screen mode
Supplied input: 1 x 1
Destination: 9 x 9
FSR 1, sharpening 0%
Presented at 1.0/s, fixed refresh.
Presented: 1.0/s average, 1% low 1.0/s, 99th percentile 1.0 ms (99 frames)
metrics: presented=118.40 low=61.20 p99=20.400 worst=31.700 frames=1024 \
client=117.90 repaints=118.20 interval=1.000 supplied=2560x1440 \
destination=3840x2160 window=supertuxkart scaling=1 selected=1 \
windowsystem=wayland buffer=gpu""".replace("\\\n", "")

# The same window before the first sampling interval has completed. The line is
# present but carries only what was known, so every measured field is absent
# rather than zero.
UNMEASURED = """Desired: Select 2560 x 1440 in the game
Supplied input: 3840 x 2160
Inactive: the window is not fullscreen or a selected borderless window covering its output.
metrics: supplied=3840x2160 destination=3840x2160 window=supertuxkart scaling=0 selected=0"""

# A build that predates the machine line, or any answer without one.
NO_CONTRACT = """Desired: Automatic (no request)
Supplied input: 3840 x 2160
Presented at 60.0/s, fixed refresh."""


def measured(value: float | None) -> float:
    """Narrow a figure the reading had to carry, so it can be compared.

    Every measured field is optional, because a key the effect did not write
    means it was not measured. A test that names one is asserting it was there,
    so absence is a failure rather than a comparison against nothing.
    """
    if value is None:
        absent = "expected a measured value, found none"
        raise AssertionError(absent)
    return value


class ParseStatusTest(unittest.TestCase):
    """The reading of one accumulated answer."""

    def test_reads_every_measured_field(self) -> None:
        """A measuring status yields the rate, its tail and the client's rate."""
        sample = parse_status(SCALING)
        self.assertAlmostEqual(measured(sample.presented_rate), 118.4)
        self.assertAlmostEqual(measured(sample.presented_low), 61.2)
        self.assertAlmostEqual(measured(sample.presented_percentile), 20.4)
        self.assertAlmostEqual(measured(sample.presented_worst), 31.7)
        self.assertEqual(sample.presented_frames, 1024)
        self.assertAlmostEqual(measured(sample.client_updates), 117.9)
        self.assertAlmostEqual(measured(sample.repaints), 118.2)
        self.assertAlmostEqual(measured(sample.interval), 1.0)

    def test_reads_the_sizes_and_the_scaling_state(self) -> None:
        """What was supplied and what it was drawn onto are both recorded."""
        sample = parse_status(SCALING)
        self.assertEqual(sample.supplied, "2560x1440")
        self.assertEqual(sample.destination, "3840x2160")
        self.assertTrue(sample.scaling)
        self.assertEqual(sample.window_system, "wayland")
        self.assertEqual(sample.buffer_kind, "gpu")

    def test_the_prose_above_the_machine_line_is_never_read(self) -> None:
        """Figures in the translated prose never reach the measurement."""
        sample = parse_status(SCALING)
        self.assertAlmostEqual(measured(sample.presented_rate), 118.4)
        self.assertEqual(sample.presented_frames, 1024)
        self.assertEqual(sample.supplied, "2560x1440")
        self.assertEqual(sample.destination, "3840x2160")

    def test_an_answer_without_the_machine_line_measures_nothing(self) -> None:
        """An older build is reported as unmeasured, not parsed from prose."""
        sample = parse_status(NO_CONTRACT)
        self.assertIsNone(sample.presented_rate)
        self.assertEqual(sample.supplied, "")

    def test_an_unmeasured_status_reports_nothing_rather_than_zero(self) -> None:
        """Absent measurements stay absent, so nothing averages a non-reading."""
        sample = parse_status(UNMEASURED)
        self.assertIsNone(sample.presented_rate)
        self.assertIsNone(sample.presented_percentile)
        self.assertIsNone(sample.client_updates)
        self.assertFalse(sample.scaling)


# A reading about some other window: the desktop, a launcher, anything the
# effect was following when the game was not there. It carries a perfectly good
# frame rate, which is exactly why it has to be thrown away.
OTHER_WINDOW = """metrics: presented=60.00 frames=1024 supplied=3840x2097 \
destination=3840x2160 window=plasmashell scaling=0 selected=0""".replace("\\\n", "")


class SummarizeTest(unittest.TestCase):
    """The reduction of a run's samples to the figures a comparison uses."""

    def samples(self, rates: list[float | None]) -> list[Sample]:
        """Build samples carrying the given rates, with one tail figure each."""
        built: list[Sample] = []
        for rate in rates:
            sample = parse_status(SCALING) if rate else parse_status(UNMEASURED)
            if rate:
                sample.presented_rate = rate
            built.append(sample)
        return built

    def test_uses_the_median_and_reports_the_spread(self) -> None:
        """One wild sample moves the spread, not the figure being compared."""
        summary = summarize("supertuxkart", "quality", self.samples([100.0, 102.0, 101.0, 60.0]))
        self.assertEqual(summary.samples, 4)
        self.assertAlmostEqual(measured(summary.presented_rate), 100.5)
        self.assertAlmostEqual(measured(summary.presented_spread), 42.0)

    def test_the_spread_is_offered_in_the_unit_a_difference_uses(self) -> None:
        """The spread is available as frame time, not only as a rate.

        A comparison states its difference in milliseconds. Judging that
        against a spread in frames per second is not a comparison at all: it
        would call a real change inconclusive, or an inconclusive one real,
        depending only on where the rates happened to sit.
        """
        summary = summarize("supertuxkart", "quality", self.samples([100.0, 50.0]))
        self.assertAlmostEqual(measured(summary.presented_spread), 50.0)
        # 1000/50 - 1000/100 = 20 - 10.
        self.assertAlmostEqual(measured(summary.frame_time_spread), 10.0)

    def test_frame_time_is_the_reciprocal_of_the_rate(self) -> None:
        """The reported frame time follows the rate it was derived from."""
        summary = summarize("supertuxkart", "native", self.samples([50.0]))
        self.assertAlmostEqual(measured(summary.frame_time), 20.0)

    def test_samples_that_measured_nothing_are_left_out(self) -> None:
        """Readings taken before the instrument had a rate do not count."""
        summary = summarize("supertuxkart", "native", self.samples([None, None, 80.0]))
        self.assertEqual(summary.samples, 1)
        self.assertAlmostEqual(measured(summary.presented_rate), 80.0)

    def test_another_window_is_not_measured_as_the_game(self) -> None:
        """A rate from a window that is not the game is discarded, not reported.

        This is the failure the check exists for: the effect follows whatever
        window it can describe, so a game that never appeared leaves the
        desktop's frame rate where the game's should have been.
        """
        summary = summarize("supertuxkart", "quality", [parse_status(OTHER_WINDOW)])
        self.assertEqual(summary.samples, 0)
        self.assertIsNone(summary.presented_rate)
        self.assertTrue(any("another window" in note for note in summary.notes))

    def test_a_run_that_measured_nothing_says_so(self) -> None:
        """An empty run reports a note rather than an invented figure."""
        summary = summarize("supertuxkart", "native", self.samples([None]))
        self.assertEqual(summary.samples, 0)
        self.assertIsNone(summary.presented_rate)
        self.assertTrue(summary.notes)


class GameReportedRateTest(unittest.TestCase):
    """The rate a game's own demo mode prints, where it prints one."""

    def test_reads_supertuxkart_profile_output(self) -> None:
        """Its profile summary gives frames and time, not a rate."""
        output = "[info   ] profile: Number of frames: 3600 time 60.0 ."
        self.assertAlmostEqual(game_reported_rate(output), 60.0)

    def test_missing_output_is_not_a_rate_of_zero(self) -> None:
        """A game that printed nothing is reported as having printed nothing."""
        self.assertIsNone(game_reported_rate("no summary here"))


if __name__ == "__main__":
    unittest.main()
