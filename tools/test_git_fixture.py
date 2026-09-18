# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""A fixture repository must not be able to write to the repository under check."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from git_fixture import LOCATIONS, detach, without_repository


class LocationTest(unittest.TestCase):
    """Every variable that names a repository has to be dropped, not some."""

    def test_named_locations(self) -> None:
        """Cover the variables Git exports to a hook."""
        self.assertIn("GIT_DIR", LOCATIONS)
        self.assertIn("GIT_WORK_TREE", LOCATIONS)
        self.assertIn("GIT_INDEX_FILE", LOCATIONS)

    def test_copy_keeps_everything_else(self) -> None:
        """Remove the locations without disturbing the rest of the environment."""
        environment = {"GIT_DIR": "/elsewhere/.git", "PATH": "/usr/bin", "HOME": "/home/user"}
        self.assertEqual(
            without_repository(environment), {"PATH": "/usr/bin", "HOME": "/home/user"}
        )
        self.assertIn("GIT_DIR", environment)

    def test_detach_clears_this_process(self) -> None:
        """A subprocess inherits the environment, so this one must be clean."""
        with mock.patch.dict(os.environ, dict.fromkeys(LOCATIONS, "/elsewhere")):
            detach()
            for name in LOCATIONS:
                self.assertNotIn(name, os.environ)


class FixtureRepositoryTest(unittest.TestCase):
    """The observed failure: a hook's GIT_DIR captured a fixture's commands."""

    def test_staging_stays_in_the_fixture(self) -> None:
        """Adding a file in a fixture must not reach the inherited repository."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            captured = root / "captured"
            fixture = root / "fixture"
            for repository in (captured, fixture):
                repository.mkdir()
                subprocess.run(["git", "init", "--quiet", str(repository)], check=True)
            (fixture / "added.txt").write_text("Text\n", encoding="utf-8")
            inherited = dict(os.environ, GIT_DIR=str(captured / ".git"))
            subprocess.run(
                ["git", "-C", str(fixture), "add", "added.txt"],
                check=True,
                env=without_repository(inherited),
            )
            staged = subprocess.run(
                ["git", "-C", str(captured), "diff", "--cached", "--name-only"],
                check=True,
                capture_output=True,
                text=True,
                env=without_repository(inherited),
            )
            self.assertEqual(staged.stdout, "")
