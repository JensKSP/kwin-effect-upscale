# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise publication gating for failed, skipped, cancelled and missing jobs."""

import copy
import unittest

from quality_gate import passed


def results(*, build: bool, packaging: bool) -> dict[str, dict[str, object]]:
    """Represent GitHub's needs context for one deliberately selected scope."""
    return {
        "commits": {
            "result": "success",
            "outputs": {"build": str(build).lower(), "packaging": str(packaging).lower()},
        },
        "checks": {"result": "success"},
        "instrumentation": {"result": "success" if build else "skipped"},
        "arm64": {"result": "success" if build else "skipped"},
        "package": {"result": "success" if packaging else "skipped"},
        "package-test": {"result": "success" if packaging else "skipped"},
    }


class QualityGateTest(unittest.TestCase):
    """A skipped required job must not turn a release green."""

    def test_selected_jobs(self) -> None:
        """Allow only deliberately skipped jobs, never failed or absent checks."""
        for build, packaging in ((True, True), (True, False), (False, False)):
            expected = results(build=build, packaging=packaging)
            self.assertTrue(passed(expected))
            for job in expected:
                for result in ("failure", "cancelled", "skipped", "success", "", None):
                    with self.subTest(build=build, packaging=packaging, job=job, result=result):
                        actual = copy.deepcopy(expected)
                        if result is None:
                            del actual[job]
                        else:
                            actual[job]["result"] = result
                        self.assertEqual(passed(actual), result == expected[job]["result"])

    def test_missing_or_invalid_scope(self) -> None:
        """A missing scope cannot grant permission to skip the expensive jobs."""
        self.assertFalse(passed({}))
        for outputs in (None, {}, {"build": "false"}, {"build": False, "packaging": "false"}):
            actual = results(build=False, packaging=False)
            actual["commits"]["outputs"] = outputs
            self.assertFalse(passed(actual))
