# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the trust and freshness boundaries of the review approval gate."""

import json
import subprocess
import unittest
from unittest.mock import call, patch

from review_approval import CODERABBIT_ID, Review, approved, refresh, refresh_all


def review(identifier: int, state: str, head: str = "current") -> Review:
    """Construct GitHub review metadata, with no trusted body text."""
    return {
        "id": identifier,
        "user": {"id": CODERABBIT_ID, "login": "coderabbitai[bot]", "type": "Bot"},
        "state": state,
        "commit_id": head,
    }


class ApprovalTest(unittest.TestCase):
    """Comments, impersonation and approvals of old code cannot satisfy the gate."""

    def test_requires_current_approval(self) -> None:
        """A completed review or approval of another revision is insufficient."""
        for state in ("COMMENTED", "PENDING", "CHANGES_REQUESTED", "DISMISSED"):
            with self.subTest(state=state):
                self.assertFalse(approved([review(1, state)], "current"))
        self.assertFalse(approved([], "current"))
        self.assertFalse(approved([review(1, "APPROVED", "old")], "current"))
        self.assertTrue(approved([review(1, "APPROVED")], "current"))

    def test_rejects_other_identities(self) -> None:
        """Only the authenticated bot account counts, regardless of display name."""
        other = review(1, "APPROVED")
        other["user"]["id"] = 1
        self.assertFalse(approved([other], "current"))
        other = review(1, "APPROVED")
        other["user"]["type"] = "User"
        self.assertFalse(approved([other], "current"))

    def test_latest_decision_wins(self) -> None:
        """Revocation blocks an old approval; later comments do not revoke it."""
        for state in ("CHANGES_REQUESTED", "DISMISSED"):
            with self.subTest(state=state):
                self.assertFalse(approved([review(2, state), review(1, "APPROVED")], "current"))
                self.assertTrue(approved([review(2, "APPROVED"), review(1, state)], "current"))
        self.assertTrue(approved([review(1, "APPROVED"), review(2, "COMMENTED")], "current"))

    def test_changed_or_closed_head(self) -> None:
        """A push or closure during review inspection invalidates the verdict."""
        initial = {"state": "open", "head": {"sha": "current"}}
        for final in (
            {"state": "open", "head": {"sha": "new"}},
            {"state": "closed", "head": {"sha": "current"}},
        ):
            with (
                self.subTest(final=final),
                patch("review_approval.api", side_effect=[initial, final]),
                patch("review_approval.pages", return_value=[review(1, "APPROVED")]),
            ):
                self.assertFalse(refresh("owner/repo", 1, "current"))

    def test_later_page_can_revoke_approval(self) -> None:
        """Read decisions beyond the first API page before publishing success."""
        pull = {"state": "open", "head": {"sha": "current"}}
        for later_state in ("APPROVED", "CHANGES_REQUESTED", "DISMISSED"):
            with (
                self.subTest(later_state=later_state),
                patch("review_approval.api", return_value=pull),
                patch(
                    "review_approval.subprocess.run",
                    return_value=subprocess.CompletedProcess(
                        ["gh", "api"],
                        0,
                        stdout=json.dumps([[review(1, "APPROVED")], [review(2, later_state)]]),
                    ),
                ) as run,
            ):
                self.assertEqual(refresh("owner/repo", 1, "current"), later_state == "APPROVED")
                command = run.call_args.args[0]
                self.assertIn("--paginate", command)
                self.assertIn("--slurp", command)

    def test_api_failure_leaves_pending(self) -> None:
        """Failure to fetch reviews must not leave a previous success in place."""
        with (
            patch(
                "review_approval.pages",
                return_value=[{"number": 1, "head": {"sha": "current"}}],
            ),
            patch(
                "review_approval.refresh",
                side_effect=subprocess.CalledProcessError(1, ["gh", "api"]),
            ),
            patch("review_approval.publish") as publish,
            self.assertRaises(subprocess.CalledProcessError),
        ):
            refresh_all("owner/repo", write=True)
        publish.assert_called_once_with("owner/repo", 1, "current", "pending")

    def test_shared_head_needs_every_pr_approved(self) -> None:
        """Two PRs for the same commit cannot overwrite each other's rejection."""
        pulls = [{"number": number, "head": {"sha": "current"}} for number in (1, 2)]
        for results in ([True, False], [False, True], [True, True]):
            with (
                self.subTest(results=results),
                patch("review_approval.pages", return_value=pulls),
                patch("review_approval.refresh", side_effect=results),
                patch("review_approval.publish") as publish,
            ):
                refresh_all("owner/repo", write=True)
                expected = [call("owner/repo", 1, "current", "pending")]
                if all(results):
                    expected.append(call("owner/repo", 1, "current", "success"))
                self.assertEqual(publish.call_args_list, expected)

    def test_dry_run_never_publishes(self) -> None:
        """Local inspection cannot write commit statuses without --publish."""
        with (
            patch(
                "review_approval.pages",
                return_value=[{"number": 1, "head": {"sha": "current"}}],
            ),
            patch("review_approval.refresh", return_value=True),
            patch("review_approval.publish") as publish,
        ):
            refresh_all("owner/repo", write=False)
        publish.assert_not_called()
