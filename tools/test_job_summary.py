# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a job summary never reports a check as passed that was not."""

import json
import runpy
import tempfile
import unittest
from pathlib import Path

SCRIPT = runpy.run_path(str(Path(__file__).with_name("job-summary.py")))
summary = SCRIPT["summary"]


class JobSummaryTest(unittest.TestCase):
    """The outcome and the reports decide what the summary says."""

    def setUp(self) -> None:
        """Give each case a build directory of its own."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.build = Path(directory.name)

    def write_tests(self, mode: str, tests: int, failures: int, skipped: int) -> None:
        """Leave a ctest JUnit report behind, as the check does."""
        report = self.build / mode / "runtime-tests.xml"
        report.parent.mkdir(parents=True)
        report.write_text(
            f'<?xml version="1.0"?><testsuite name="upscale" tests="{tests}" '
            f'failures="{failures}" disabled="0" skipped="{skipped}"></testsuite>'
        )

    def test_only_success_is_passed(self) -> None:
        """Skipped, cancelled and unknown outcomes are never passed."""
        for outcome, said in (
            ("success", "Checks: passed"),
            ("failure", "Checks: **failed**"),
            ("skipped", "Checks: skipped, not run"),
            ("cancelled", "Checks: cancelled"),
            ("", "Checks: unknown (no outcome)"),
        ):
            with self.subTest(outcome=outcome):
                text = summary("gcc", outcome, "trixie", "X64", self.build)
                self.assertIn(said, text)
                if outcome != "success":
                    self.assertNotIn("passed", text)

    def test_counts_the_tests(self) -> None:
        """The JUnit report's figures are what the summary states."""
        self.write_tests("gcc", 31, 1, 2)
        self.assertIn(
            "Tests: 31 run, 1 failed, 2 skipped",
            summary("gcc", "failure", "trixie", "X64", self.build),
        )

    def test_missing_report_is_said(self) -> None:
        """No report is no report, not a clean run."""
        self.assertIn(
            "Tests: no test report", summary("clang", "success", "trixie", "X64", self.build)
        )

    def test_build_only_says_so(self) -> None:
        """A platform that is only built did not run its tests, and says so."""
        text = summary("gcc", "success", "neon-unstable", "X64", self.build, build_only=True)
        self.assertIn("Tests: not run, this platform is only built", text)
        self.assertNotIn("no test report", text)

    def test_coverage(self) -> None:
        """Coverage comes from gcovr's summary."""
        self.write_tests("coverage", 31, 0, 0)
        report = self.build / "coverage" / "coverage" / "summary.json"
        report.parent.mkdir(parents=True)
        report.write_text(json.dumps({"line_percent": 91.234}))
        self.assertIn(
            "Coverage: 91.2 % of lines", summary("coverage", "success", "trixie", "X64", self.build)
        )


if __name__ == "__main__":
    unittest.main()
