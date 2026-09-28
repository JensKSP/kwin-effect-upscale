# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that hook updates take the newest versions, except where one is held."""

import os
import runpy
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from git_fixture import detach

SCRIPT = runpy.run_path(str(Path(__file__).with_name("update-hooks.py")))
pins = SCRIPT["pins"]
update = SCRIPT["update"]
report = SCRIPT["report"]

MANIFEST = "- id: fixture\n  name: fixture\n  entry: fixture\n  language: fail\n"


def setUpModule() -> None:
    """Keep fixture repositories out of the repository this check is running for."""
    detach()


@unittest.skipUnless(shutil.which("pre-commit"), "needs pre-commit, which the check image has")
class UpdateHooksTest(unittest.TestCase):
    """Two hook repositories, one free and one held, beside a local hook."""

    def setUp(self) -> None:
        """Tag two repositories and pin both to their oldest tag."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        environment = {
            "GIT_AUTHOR_NAME": "fixture",
            "GIT_AUTHOR_EMAIL": "fixture@invalid",
            "GIT_COMMITTER_NAME": "fixture",
            "GIT_COMMITTER_EMAIL": "fixture@invalid",
            "PRE_COMMIT_HOME": str(self.root / "cache"),
        }
        patcher = mock.patch.dict(os.environ, environment)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.free = self.repository("free", ["v1.0.0", "v1.2.0"])
        # The newest 19 is not the newest tag, and a candidate after it is no
        # release to move to.
        self.held = self.repository("held", ["v19.1.7", "v19.1.8", "v20.1.0", "v19.1.9-rc1"])
        # Its newest release is tagged off the default branch, whose own newest
        # tag is older.
        self.ahead = self.repository("ahead", ["v8.30.0"], aside="v8.30.1")
        self.config = self.root / "config.yaml"
        self.config.write_text(
            "repos:\n"
            f"  - repo: {self.free}\n    rev: v1.0.0\n    hooks:\n      - id: fixture\n"
            f"  - repo: {self.held}\n    rev: v19.1.7  # held to v19.\n"
            "    hooks:\n      - id: fixture\n"
            f"  - repo: {self.ahead}\n    rev: v8.30.1\n    hooks:\n      - id: fixture\n"
            "  - repo: local\n    hooks:\n      - id: own\n        name: own\n"
            "        entry: own\n        language: fail\n"
        )

    def repository(self, name: str, tags: list[str], aside: str = "") -> str:
        """Make a hook repository with one commit per tag, in the order given.

        A tag aside is made on a branch of its own, which the default branch
        does not contain.
        """
        path = self.root / name
        path.mkdir()
        git = ["git", "-C", str(path)]
        subprocess.run([*git, "init", "--quiet"], check=True)
        (path / ".pre-commit-hooks.yaml").write_text(MANIFEST)
        subprocess.run([*git, "add", "."], check=True)
        for tag in tags:
            subprocess.run([*git, "commit", "--quiet", "--allow-empty", "-m", tag], check=True)
            subprocess.run([*git, "tag", tag], check=True)
        if aside:
            subprocess.run([*git, "checkout", "--quiet", "-b", "aside"], check=True)
            subprocess.run([*git, "commit", "--quiet", "--allow-empty", "-m", aside], check=True)
            subprocess.run([*git, "tag", aside], check=True)
            subprocess.run([*git, "checkout", "--quiet", "-"], check=True)
        return str(path)

    def test_the_free_hook_takes_its_newest_tag(self) -> None:
        """pre-commit's own update moves a free hook to its newest release."""
        update(self.config)
        self.assertEqual(pins(self.config.read_text())[self.free], ("v1.2.0", ""))

    def test_the_held_hook_stays_in_its_series(self) -> None:
        """A held hook moves to the newest release of its series, not past it."""
        moved = update(self.config)
        self.assertEqual(pins(self.config.read_text())[self.held], ("v19.1.8", "v19."))
        self.assertIn((self.held, "v19.1.7", "v19.1.8"), moved)
        self.assertIn("rev: v19.1.8  # held to v19.", self.config.read_text())

    def test_a_pin_never_moves_back(self) -> None:
        """A default branch whose newest tag is older than the pin leaves it be."""
        moved = update(self.config)
        self.assertEqual(pins(self.config.read_text())[self.ahead], ("v8.30.1", ""))
        self.assertNotIn(self.ahead, [repository for repository, _, _ in moved])

    def test_local_hooks_are_left_alone(self) -> None:
        """A local hook has no version to update, and keeps its lines."""
        update(self.config)
        self.assertIn("  - repo: local\n    hooks:\n      - id: own\n", self.config.read_text())
        self.assertNotIn("local", pins(self.config.read_text()))

    def test_the_report_names_what_moved(self) -> None:
        """The summary is a table of what moved, or says nothing did."""
        moved = update(self.config)
        self.assertIn(f"| {self.free} | v1.0.0 | v1.2.0 |", report(moved))
        self.assertEqual(update(self.config), [])
        self.assertEqual(report([]), "Every pinned hook is at its newest version.\n")


if __name__ == "__main__":
    unittest.main()
