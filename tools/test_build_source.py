# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what a source archive records of where its sources came from."""

import os
import runpy
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from git_fixture import detach

SCRIPT = runpy.run_path(str(Path(__file__).with_name("build-source.py")))
provenance = SCRIPT["provenance"]


def setUpModule() -> None:
    """Keep fixture repositories out of the repository this check is running for."""
    detach()


class ProvenanceTest(unittest.TestCase):
    """A commit, its branch and its tag are recorded; nothing is guessed."""

    def setUp(self) -> None:
        """Make a repository of one commit on a branch, and work inside it."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.addCleanup(os.chdir, Path.cwd())
        os.chdir(directory.name)
        environment = {
            "GIT_AUTHOR_NAME": "fixture",
            "GIT_AUTHOR_EMAIL": "fixture@invalid",
            "GIT_COMMITTER_NAME": "fixture",
            "GIT_COMMITTER_EMAIL": "fixture@invalid",
        }
        patcher = mock.patch.dict(os.environ, environment)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name in ("GITHUB_HEAD_REF", "GITHUB_REF_NAME", "GITHUB_REF_TYPE"):
            os.environ.pop(name, None)
        self.git("init", "--quiet", "--initial-branch=release/0.1.0")
        Path("README").write_text("fixture\n")
        self.git("add", "README")
        self.git("commit", "--quiet", "-m", "fixture")
        self.commit = self.git("rev-parse", "HEAD")

    @staticmethod
    def git(*arguments: str) -> str:
        """Run Git in the fixture repository."""
        return subprocess.run(
            ["git", *arguments], check=True, capture_output=True, text=True
        ).stdout.strip()

    def test_commit_and_branch(self) -> None:
        """A commit checked out on a branch records both."""
        self.assertEqual(provenance("HEAD"), {"commit": self.commit, "branch": "release/0.1.0"})

    def test_tag_on_the_commit(self) -> None:
        """A tag on the commit is recorded beside the branch."""
        self.git("tag", "v0.1.0")
        self.assertEqual(provenance("HEAD")["tag"], "v0.1.0")
        self.assertEqual(provenance("HEAD")["branch"], "release/0.1.0")

    def test_forge_tag_build(self) -> None:
        """A forge's build for a tag records the tag and no branch."""
        self.git("tag", "v0.1.0")
        os.environ["GITHUB_REF_NAME"] = "v0.1.0"
        os.environ["GITHUB_REF_TYPE"] = "tag"
        self.assertEqual(provenance("HEAD"), {"commit": self.commit, "tag": "v0.1.0"})

    def test_forge_tag_of_another_commit(self) -> None:
        """A tag the forge built for is not recorded for a commit it is not on."""
        self.git("tag", "v0.1.0")
        Path("README").write_text("later\n")
        self.git("commit", "--quiet", "-am", "later")
        os.environ["GITHUB_REF_NAME"] = "v0.1.0"
        os.environ["GITHUB_REF_TYPE"] = "tag"
        recorded = provenance("HEAD")
        self.assertEqual(recorded["commit"], self.git("rev-parse", "HEAD"))
        self.assertNotIn("tag", recorded)
        self.assertNotEqual(recorded.get("branch"), "v0.1.0")

    def test_tree_records_nothing(self) -> None:
        """A tree archived for a local check is no commit, and says nothing."""
        self.assertEqual(provenance(self.git("rev-parse", "HEAD^{tree}")), {})


if __name__ == "__main__":
    unittest.main()
