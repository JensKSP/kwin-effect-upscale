# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise publication gating for failed, skipped, cancelled and missing jobs."""

import unittest

from quality_gate import passed


class QualityGateTest(unittest.TestCase):
    """A skipped required job must not turn a release green."""

    def test_required_jobs(self) -> None:
        """Reject every unsuccessful or missing required job."""
        for job in ("commits", "checks", "instrumentation", "package-smoke"):
            for result in ("failure", "cancelled", "skipped", "", None):
                with self.subTest(job=job, result=result):
                    results: dict[str, dict[str, object]] = {
                        name: {"result": "success"}
                        for name in ("commits", "checks", "instrumentation", "package-smoke")
                    }
                    if result is None:
                        del results[job]
                    else:
                        results[job]["result"] = result
                    self.assertEqual(
                        passed(results), job == "package-smoke" and result == "skipped"
                    )

    def test_success(self) -> None:
        """Accept a fully successful supported-platform candidate."""
        results: dict[str, dict[str, object]] = {
            name: {"result": "success"}
            for name in ("commits", "checks", "instrumentation", "package-smoke")
        }
        self.assertTrue(passed(results))
        self.assertFalse(passed({}))
