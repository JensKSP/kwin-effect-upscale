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
# The prose is translated and deliberately not read; only the machine line is.
# It is reproduced here in German to prove that, because a parser that quietly
# depended on English would pass a test written only in English.
SCALING = """Gewünscht: 2560 × 1440 von SuperTuxKart als Bildschirmmodus angefordert
Geliefertes Bild: 2560 × 1440
Ziel: 3840 × 2160
FSR 1, Schärfung 0%
metrics: presented=118.40 low=61.20 p99=20.400 worst=31.700 frames=1024 \
client=117.90 repaints=118.20 interval=1.000 supplied=2560x1440 \
destination=3840x2160 scaling=1 selected=1 windowsystem=wayland buffer=gpu""".replace("\\\n", "")

# The same window before the first sampling interval has completed. The line is
# present but carries only what was known, so every measured field is absent
# rather than zero.
UNMEASURED = """Desired: Select 2560 × 1440 in the game
Supplied input: 3840 × 2160
Destination: 3840 × 2160
Inactive: the window is not fullscreen or a selected borderless window covering its output.
metrics: supplied=3840x2160 destination=3840x2160 scaling=0 selected=0"""

# A build that predates the machine line, or any answer without one.
NO_CONTRACT = """Desired: Automatic (no request)
Supplied input: 3840 × 2160
Presentation is not being measured."""


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
        self.assertAlmostEqual(sample.interval, 1.0)

    def test_reads_the_sizes_and_the_scaling_state(self) -> None:
        """What was supplied and what it was drawn onto are both recorded."""
        sample = parse_status(SCALING)
        self.assertEqual(sample.supplied, "2560x1440")
        self.assertEqual(sample.destination, "3840x2160")
        self.assertTrue(sample.scaling)
        self.assertEqual(sample.window_system, "wayland")
        self.assertEqual(sample.buffer_kind, "gpu")

    def test_a_translated_session_is_read_exactly_the_same(self) -> None:
        """The language of the prose above the machine line changes nothing."""
        sample = parse_status(SCALING)
        self.assertAlmostEqual(sample.presented_rate, 118.4)
        self.assertEqual(sample.presented_frames, 1024)

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
