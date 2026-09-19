# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for reading the effect's measurements out of its status."""

import runpy
import unittest
from pathlib import Path

HARNESS = runpy.run_path(str(Path(__file__).with_name("measure-frame-times.py")))
parse_status = HARNESS["parse_status"]
summarize = HARNESS["summarize"]
game_reported_rate = HARNESS["game_reported_rate"]
Sample = HARNESS["Sample"]

# A status exactly as the effect answers it while it is scaling and measuring.
# The separator between the two numbers of a size is a multiplication sign, not
# an "x", which is the detail a parser written from memory gets wrong.
SCALING = """Desired: Select 2560 × 1440 in the game
Supplied input: 2560 × 1440
Destination: 3840 × 2160
FSR 1, sharpening 0%
Presented at 118.4/s, fixed refresh.
Presented: 118.4/s average, 1% low 61.2/s, 99th percentile 20.4 ms, worst 31.7 ms (1024 frames, fixed refresh)
Client buffer updates: 117.9/s, compositor repaints: 118.2/s (1.0 s sample, 0.3 s ago)
HDR follows KWin colour management."""

# The same window before the first sampling interval has completed. Every
# measured field is absent rather than zero, which is the distinction the
# summary depends on.
UNMEASURED = """Desired: Select 2560 × 1440 in the game
Supplied input: 3840 × 2160
Destination: 3840 × 2160
Inactive: the window is not fullscreen or a selected borderless window covering its output.
Presentation is not being measured; the on-screen display measures it while it is shown.
HDR follows KWin colour management."""


class ParseStatusTest(unittest.TestCase):
    """The reading of one accumulated answer."""

    def test_reads_every_measured_field(self) -> None:
        """A measuring status yields the rate, its tail and the client's rate."""
        sample = parse_status(SCALING)
        self.assertAlmostEqual(sample.presented_rate, 118.4)
        self.assertAlmostEqual(sample.presented_low, 61.2)
        self.assertAlmostEqual(sample.presented_percentile, 20.4)
        self.assertAlmostEqual(sample.presented_worst, 31.7)
        self.assertEqual(sample.presented_frames, 1024)
        self.assertAlmostEqual(sample.client_updates, 117.9)
        self.assertAlmostEqual(sample.repaints, 118.2)
        self.assertAlmostEqual(sample.sample_age, 0.3)

    def test_reads_the_sizes_and_the_scaling_state(self) -> None:
        """What was supplied and what it was drawn onto are both recorded."""
        sample = parse_status(SCALING)
        self.assertEqual(sample.supplied, "2560x1440")
        self.assertEqual(sample.destination, "3840x2160")
        self.assertTrue(sample.scaling)

    def test_an_unmeasured_status_reports_nothing_rather_than_zero(self) -> None:
        """Absent measurements stay absent, so nothing averages a non-reading."""
        sample = parse_status(UNMEASURED)
        self.assertIsNone(sample.presented_rate)
        self.assertIsNone(sample.presented_percentile)
        self.assertIsNone(sample.client_updates)
        self.assertFalse(sample.scaling)
        self.assertIn("not fullscreen", sample.refusal)

    def test_the_average_line_is_not_confused_with_the_summary_sentence(self) -> None:
        """"Presented at" and "Presented:" are different lines carrying a rate."""
        sample = parse_status(SCALING)
        self.assertAlmostEqual(sample.presented_rate, 118.4)
        self.assertNotEqual(sample.presented_frames, 0)


class SummarizeTest(unittest.TestCase):
    """The reduction of a run's samples to the figures a comparison uses."""

    def samples(self, rates: list[float | None]) -> list:
        """Samples carrying the given rates, with one tail figure each."""
        built = []
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
        self.assertAlmostEqual(summary.presented_rate, 100.5)
        self.assertAlmostEqual(summary.presented_spread, 42.0)

    def test_frame_time_is_the_reciprocal_of_the_rate(self) -> None:
        """The reported frame time follows the rate it was derived from."""
        summary = summarize("supertuxkart", "native", self.samples([50.0]))
        self.assertAlmostEqual(summary.frame_time, 20.0)

    def test_samples_that_measured_nothing_are_left_out(self) -> None:
        """Readings taken before the instrument had a rate do not count."""
        summary = summarize("supertuxkart", "native", self.samples([None, None, 80.0]))
        self.assertEqual(summary.samples, 1)
        self.assertAlmostEqual(summary.presented_rate, 80.0)

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
