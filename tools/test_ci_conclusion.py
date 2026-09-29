# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that the nightly relies only on a CI run that finished."""

import runpy
import subprocess
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = runpy.run_path(str(Path(__file__).with_name("ci-conclusion.py")))
state = SCRIPT["state"]


def run(status: str, conclusion: str, created: str) -> dict[str, str]:
    """One CI run as GitHub lists it, with only the fields that matter."""
    return {"status": status, "conclusion": conclusion, "created_at": created}


class ConclusionTest(unittest.TestCase):
    """The newest finished run decides; nothing unfinished counts."""

    def test_a_green_run_passes(self) -> None:
        """The commit's CI finished green: the nightly need not run it again."""
        self.assertEqual(state([run("completed", "success", "2026-09-29T01:00:00Z")]), "passed")

    def test_a_stalled_api_request_reports_failure(self) -> None:
        """A request timeout follows the API-error path instead of hanging the job."""
        with (
            mock.patch("subprocess.run", side_effect=subprocess.TimeoutExpired("gh", 120)) as api,
            self.assertRaisesRegex(RuntimeError, "API request timed out"),
        ):
            SCRIPT["runs_of"]("owner/repository", "revision")
        self.assertEqual(api.call_args.kwargs["timeout"], 120)

    def test_no_run_is_missing(self) -> None:
        """A commit that never went through CI has the nightly run it."""
        self.assertEqual(state([]), "missing")

    def test_an_unfinished_run_is_missing(self) -> None:
        """A run still going, or cancelled, says nothing yet."""
        runs = [
            run("in_progress", "", "2026-09-29T02:00:00Z"),
            run("completed", "cancelled", "2026-09-29T01:00:00Z"),
        ]
        self.assertEqual(state(runs), "missing")

    def test_the_newest_finished_run_decides(self) -> None:
        """A rerun that went green after a red one passes, and the other way round."""
        red_then_green = [
            run("completed", "failure", "2026-09-29T01:00:00Z"),
            run("completed", "success", "2026-09-29T02:00:00Z"),
        ]
        self.assertEqual(state(red_then_green), "passed")
        green_then_red = [
            run("completed", "success", "2026-09-29T01:00:00Z"),
            run("completed", "failure", "2026-09-29T02:00:00Z"),
        ]
        self.assertEqual(state(green_then_red), "failed")


if __name__ == "__main__":
    unittest.main()
