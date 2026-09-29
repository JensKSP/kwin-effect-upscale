# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that two package builds that differ are told apart file by file."""

import os
import runpy
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = runpy.run_path(str(Path(__file__).with_name("build-deb-package.py")))
differences = SCRIPT["differences"]


# The time the fixture packages are built at.
EPOCH = 1_700_000_000


@unittest.skipUnless(shutil.which("dpkg-deb"), "needs dpkg-deb, which the check image has")
class DifferencesTest(unittest.TestCase):
    """What a failed reproducibility comparison says about the two builds."""

    def setUp(self) -> None:
        """Give each case a directory of its own."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def package(self, build: str, library: bytes) -> Path:
        """Build a package holding a library and a document."""
        tree = self.root / build / "tree"
        (tree / "DEBIAN").mkdir(parents=True)
        (tree / "DEBIAN/control").write_text(
            "Package: fixture\nVersion: 1\nArchitecture: all\n"
            "Maintainer: Test <test@example.org>\nDescription: Fixture\n"
        )
        (tree / "usr/lib").mkdir(parents=True)
        (tree / "usr/lib/upscale.so").write_bytes(library)
        (tree / "usr/share").mkdir(parents=True)
        (tree / "usr/share/notices.md").write_text("the same in both\n")
        # Built as the package is, from fixed times: two builds a second apart
        # otherwise record different times in their archives, which a slow
        # runner showed on 2026-09-29 where a fast machine never had.
        for path in [tree, *tree.rglob("*")]:
            os.utime(path, (EPOCH, EPOCH))
        package = self.root / build / "fixture_1_all.deb"
        subprocess.run(
            ["dpkg-deb", "--build", "--root-owner-group", str(tree), str(package)],
            check=True,
            capture_output=True,
            env=os.environ | {"SOURCE_DATE_EPOCH": str(EPOCH)},
        )
        return package

    def test_the_differing_file_is_named(self) -> None:
        """Only the file whose bytes differ is listed, under its package."""
        first = self.package("first", b"one")
        second = self.package("second", b"two")
        found = differences([first], [second], self.root / "scratch")
        self.assertEqual(found, ["fixture_1_all.deb: usr/lib/upscale.so"])

    def test_identical_builds_say_nothing(self) -> None:
        """Packages with the same bytes are not listed."""
        first = self.package("first", b"one")
        second = self.package("second", b"one")
        self.assertEqual(differences([first], [second], self.root / "scratch"), [])


if __name__ == "__main__":
    unittest.main()
