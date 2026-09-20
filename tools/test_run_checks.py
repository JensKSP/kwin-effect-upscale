# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the container dependency stamp."""

import hashlib
import runpy
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERIFY = runpy.run_path(str(Path(__file__).with_name("run-checks.py")))[
    "verify_container_dependencies"
]

CONTAINERFILES = ("trixie", "neon-unstable", "package")


class DependencyStampTest(unittest.TestCase):
    """A cached image that predates a build dependency must say so itself."""

    def stamp(self, directory: Path, contents: str | None) -> None:
        """Point the check at a stamp this test controls, or at none at all."""
        path = directory / "upscale-dependency-stamp"
        if contents is not None:
            path.write_text(contents)
        VERIFY.__globals__["DEPENDENCY_STAMP"] = path

    def test_absent_stamp_is_not_a_container(self) -> None:
        """Outside the maintained images there is nothing to compare against."""
        with tempfile.TemporaryDirectory() as directory:
            self.stamp(Path(directory), None)
            VERIFY(ROOT)

    def test_matching_stamp_passes(self) -> None:
        """An image built from this debian/control runs the checks."""
        digest = hashlib.sha256((ROOT / "debian" / "control").read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            # sha256sum writes the digest, two spaces and the file name.
            self.stamp(Path(directory), f"{digest}  /tmp/upscale-debian-control\n")
            VERIFY(ROOT)

    def test_stale_stamp_names_the_cause_and_the_remedy(self) -> None:
        """The message has to name the image, not a missing header much later."""
        with tempfile.TemporaryDirectory() as directory:
            self.stamp(Path(directory), f"{'0' * 64}  /tmp/upscale-debian-control\n")
            with self.assertRaises(SystemExit) as failure:
                VERIFY(ROOT)
        message = str(failure.exception)
        self.assertIn("debian/control", message)
        self.assertIn("podman build", message)

    def test_every_maintained_image_writes_the_stamp(self) -> None:
        """An image without a stamp would silently opt out of the check."""
        for name in CONTAINERFILES:
            with self.subTest(container=name):
                text = (ROOT / "containers" / name / "Containerfile").read_text()
                self.assertIn("/etc/upscale-dependency-stamp", text)


if __name__ == "__main__":
    unittest.main()
