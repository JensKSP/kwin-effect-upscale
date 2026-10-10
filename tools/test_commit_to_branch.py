# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for committing onto a branch without touching the checkout."""

import os
import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from git_fixture import without_repository

TOOL = runpy.run_path(str(Path(__file__).with_name("commit-to-branch.py")))
commit = TOOL["commit"]


class CommitToBranchTest(unittest.TestCase):
    """Commit onto a branch of a fixture repository while another is checked out."""

    def setUp(self) -> None:
        """Make a repository on main with a work branch, and a copy with changes."""
        self.scratch = tempfile.TemporaryDirectory()
        self.root = Path(self.scratch.name)
        self.repository = self.root / "repository"
        self.copy = self.root / "copy"
        self.environment = without_repository(dict(os.environ)) | {
            "GIT_AUTHOR_NAME": "Fixture",
            "GIT_AUTHOR_EMAIL": "fixture@example.invalid",
            "GIT_COMMITTER_NAME": "Fixture",
            "GIT_COMMITTER_EMAIL": "fixture@example.invalid",
        }
        # The tool runs git with the process's own environment, as it does for
        # a person, so the fixture's identity goes there for the test alone.
        self.saved = dict(os.environ)
        os.environ.clear()
        os.environ.update(self.environment)
        self.git("init", "-q", "-b", "main", str(self.repository), where=self.root)
        (self.repository / "kept.txt").write_text("kept\n")
        (self.repository / "gone.txt").write_text("gone\n")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "start")
        self.git("branch", "work")
        self.copy.mkdir()
        (self.copy / "kept.txt").write_text("changed\n")
        (self.copy / "new.sh").write_text("#!/bin/sh\n")
        (self.copy / "new.sh").chmod(0o755)
        self.message = self.root / "message"
        self.message.write_text("Change the work branch\n\nBody.\n")

    def tearDown(self) -> None:
        """Give the process its own environment back and remove the fixture."""
        os.environ.clear()
        os.environ.update(self.saved)
        self.scratch.cleanup()

    def git(self, *arguments: str, where: Path | None = None) -> str:
        """Run git in the fixture as the fixture's identity."""
        return subprocess.run(
            ["git", "-C", str(where or self.repository), *arguments],
            capture_output=True,
            text=True,
            env=self.environment,
            check=True,
        ).stdout.strip()

    def test_commits_onto_the_branch_and_leaves_the_checkout(self) -> None:
        """Change, add and remove on the branch; the checkout stays as it was."""
        commit(self.repository, "work", self.message, self.copy, ["kept.txt", "new.sh", "gone.txt"])
        self.assertEqual(self.git("show", "work:kept.txt"), "changed")
        self.assertIn("100755", self.git("ls-tree", "work", "new.sh"))
        self.assertEqual(self.git("ls-tree", "--name-only", "work", "gone.txt"), "")
        self.assertEqual(self.git("log", "-1", "--format=%s", "work"), "Change the work branch")
        # The checkout is on main, untouched: its files, its index and its branch.
        self.assertEqual((self.repository / "kept.txt").read_text(), "kept\n")
        self.assertEqual(self.git("status", "--porcelain"), "")
        self.assertEqual(self.git("rev-parse", "--abbrev-ref", "HEAD"), "main")
        self.assertEqual(self.git("rev-parse", "main"), self.git("rev-parse", "work~1"))

    def test_ignores_an_inherited_repository(self) -> None:
        """Commit to the named repository even when a hook's GIT_DIR names another."""
        other = self.root / "other"
        self.git("init", "-q", "-b", "main", str(other), where=self.root)
        os.environ["GIT_DIR"] = str(other / ".git")
        commit(self.repository, "work", self.message, self.copy, ["kept.txt"])
        del os.environ["GIT_DIR"]
        self.assertEqual(self.git("show", "work:kept.txt"), "changed")
        self.assertEqual(self.git("for-each-ref", "refs/heads/work", where=other), "")

    def test_refuses_an_empty_commit(self) -> None:
        """Refuse paths that the branch already has as the copy does."""
        (self.copy / "kept.txt").write_text("kept\n")
        with self.assertRaises(RuntimeError):
            commit(self.repository, "work", self.message, self.copy, ["kept.txt"])

    def test_refuses_a_missing_branch(self) -> None:
        """Refuse a branch the repository does not have."""
        with self.assertRaises(RuntimeError):
            commit(self.repository, "absent", self.message, self.copy, ["kept.txt"])


if __name__ == "__main__":
    unittest.main()
