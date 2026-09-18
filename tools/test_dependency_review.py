# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the dependency policy against recorded advisories and exceptions."""

import json
import tempfile
import unittest
from pathlib import Path
from typing import cast

from dependency_review import Advisory, Change, blocking, introduced, rank, review, summarise


def advisory(severity: str, identifier: str = "GHSA-0000-0000-0000") -> Advisory:
    """One advisory as GitHub's dependency graph comparison reports it."""
    return {
        "severity": severity,
        "advisory_ghsa_id": identifier,
        "advisory_summary": "Recorded for the policy tests",
        "advisory_url": f"https://github.com/advisories/{identifier}",
    }


def change(*, added: bool = True, vulnerabilities: list[Advisory] | None = None) -> Change:
    """One dependency change in a manifest the graph recognises."""
    return {
        "change_type": "added" if added else "removed",
        "manifest": ".github/workflows/ci.yml",
        "ecosystem": "actions",
        "name": "example/action",
        "version": "v1",
        "vulnerabilities": vulnerabilities or [],
    }


class SeverityTest(unittest.TestCase):
    """An unreadable severity must never be quieter than a readable one."""

    def test_order(self) -> None:
        """Rank GitHub's severities, case and spacing aside."""
        self.assertLess(rank("low"), rank("moderate"))
        self.assertLess(rank("moderate"), rank("high"))
        self.assertLess(rank("high"), rank("critical"))
        self.assertEqual(rank(" Critical "), rank("critical"))

    def test_unknown_severity_blocks(self) -> None:
        """Place an absent or misspelled severity above critical, not below low."""
        self.assertGreater(rank(""), rank("critical"))
        self.assertGreater(rank("severe"), rank("critical"))
        self.assertTrue(blocking([change(vulnerabilities=[advisory("severe")])]))


class PolicyTest(unittest.TestCase):
    """The threshold, the exceptions and the change type each decide alone."""

    def test_threshold(self) -> None:
        """Block from the threshold upwards and leave what is below it alone."""
        for severity, blocked in (
            ("low", False),
            ("moderate", True),
            ("high", True),
            ("critical", True),
        ):
            with self.subTest(severity=severity):
                changes = [change(vulnerabilities=[advisory(severity)])]
                self.assertEqual(bool(blocking(changes)), blocked)

    def test_removed_dependencies_are_not_reviewed(self) -> None:
        """A dependency being removed carries its advisory away with it."""
        changes = [change(added=False, vulnerabilities=[advisory("critical")])]
        self.assertEqual(introduced(changes), [])
        self.assertFalse(blocking(changes))

    def test_recorded_exception(self) -> None:
        """Only the recorded identifier is accepted, and only that one."""
        changes = [change(vulnerabilities=[advisory("high", "GHSA-aaaa-bbbb-cccc")])]
        self.assertFalse(blocking(changes, exceptions={"GHSA-aaaa-bbbb-cccc": "assessed"}))
        self.assertTrue(blocking(changes, exceptions={"GHSA-dddd-eeee-ffff": "assessed"}))

    def test_several_advisories_on_one_dependency(self) -> None:
        """Report every blocking advisory, not merely the first one found."""
        changes = [
            change(
                vulnerabilities=[
                    advisory("low", "GHSA-1111-1111-1111"),
                    advisory("high", "GHSA-2222-2222-2222"),
                    advisory("critical", "GHSA-3333-3333-3333"),
                ]
            )
        ]
        blocked = blocking(changes)
        self.assertEqual(
            [found["advisory_ghsa_id"] for _, found in blocked],
            ["GHSA-2222-2222-2222", "GHSA-3333-3333-3333"],
        )


class ReportTest(unittest.TestCase):
    """An empty review must read as "nothing was recognised", not as a pass."""

    def test_empty_comparison(self) -> None:
        """Say that no addition was recognised rather than reporting success."""
        self.assertIn("No dependency additions", summarise([]))
        self.assertTrue(review([]))

    def test_summary_names_every_addition(self) -> None:
        """Name the ecosystem, dependency and manifest a reviewer has to check."""
        summary = summarise([change(), change(added=False)])
        self.assertIn("Recognised 1 added dependencies", summary)
        self.assertIn("actions example/action v1", summary)

    def test_fixture_file_fails_the_policy(self) -> None:
        """A recorded comparison exercises the same code path a pull request uses."""
        changes = [change(vulnerabilities=[advisory("high", "GHSA-4444-4444-4444")])]
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory) / "changes.json"
            fixture.write_text(json.dumps(changes), encoding="utf-8")
            recorded = cast("list[Change]", json.loads(fixture.read_text(encoding="utf-8")))
        self.assertFalse(review(recorded))
