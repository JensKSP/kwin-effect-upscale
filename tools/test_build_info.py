# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check version identity using real Git and the build-time CMake generator."""

import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

from git_fixture import detach

ROOT = Path(__file__).resolve().parent.parent


def setUpModule() -> None:
    """Keep fixture repositories out of the repository this check is running for."""
    detach()


class BuildInfoTest(unittest.TestCase):
    """Use a disposable clone without creating commits or changing identities."""

    def setUp(self) -> None:
        """Clone committed sources while using the current generator under test."""
        if not (ROOT / ".git").exists():
            self.skipTest("Git identity tests require a checkout")
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.source = self.root / "source"
        self.output = self.root / "buildinfo.cpp"
        self.run_command("git", "clone", "--quiet", "--no-hardlinks", str(ROOT), str(self.source))
        tags = self.git("tag").splitlines()
        if tags:
            self.git("tag", "--delete", *tags)
        self.environment = os.environ.copy()
        for name in (
            "GITHUB_HEAD_REF",
            "GITHUB_REF_NAME",
            "CI_COMMIT_REF_NAME",
            "SOURCE_DATE_EPOCH",
        ):
            self.environment.pop(name, None)

    def run_command(self, *arguments: str, env: dict[str, str] | None = None) -> str:
        """Run a tool and surface its diagnostics on failure."""
        return subprocess.run(
            arguments,
            check=True,
            capture_output=True,
            text=True,
            env=env,
        ).stdout.strip()

    def git(self, *arguments: str) -> str:
        """Run Git only in the disposable checkout."""
        return self.run_command("git", "-C", str(self.source), *arguments)

    def generate(self, *definitions: str) -> str:
        """Generate the production translation unit with optional overrides."""
        self.run_command(
            "cmake",
            f"-DSOURCE_DIR={self.source}",
            f"-DTEMPLATE={ROOT / 'src/buildinfo/buildinfo.cpp.in'}",
            f"-DOUTPUT={self.output}",
            "-DPROJECT_VERSION=0.1.0",
            *definitions,
            "-P",
            str(ROOT / "cmake/GenerateBuildInfo.cmake"),
            env=self.environment,
        )
        return self.output.read_text()

    def test_tags(self) -> None:
        """Only the matching release tag suppresses the Git suffix."""
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "nightly")
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "v9.9.9")
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "v0.1.0")
        self.assertIn('QStringLiteral("0.1.0")', self.generate())
        with (self.source / "README.md").open("a") as stream:
            stream.write("\nChanged source.\n")
        self.assertIn("-dirty", self.generate())

    def test_package_version(self) -> None:
        """The package's explicit version wins over tracked packaging edits."""
        with (self.source / "README.md").open("a") as stream:
            stream.write("\nPackaging edit.\n")
        self.assertIn(
            'QStringLiteral("0.1.0~trixie")', self.generate("-DPACKAGE_VERSION=0.1.0~trixie")
        )

    def test_archive_version(self) -> None:
        """An extracted archive retains the snapshot version without Git."""
        metadata = self.root / "source-version"
        metadata.write_text("# archive metadata\n0.1.0+git20260917.0123456789\n")
        archive = self.root / "source.tar.gz"
        self.git(
            "archive",
            "--format=tar.gz",
            "--prefix=upscale/",
            f"--add-file={metadata}",
            f"--output={archive}",
            "HEAD",
        )
        with tarfile.open(archive) as tar:
            stream = tar.extractfile("upscale/source-version")
            if stream is None:
                self.fail("the archive does not carry the version metadata")
            version = stream.read()
        shutil.rmtree(self.source / ".git")
        (self.source / "source-version").write_bytes(version)
        self.assertIn('QStringLiteral("0.1.0+git20260917.0123456789")', self.generate())

    def test_unchanged_build(self) -> None:
        """An unchanged build retains its original timestamp and source mtime."""
        self.generate()
        state = self.output.with_suffix(".cpp.state")
        fingerprint = state.read_text().splitlines()[0]
        state.write_text(f"{fingerprint}\n2000-01-01T00:00:00Z\n")
        first = self.generate()
        modification_time = self.output.stat().st_mtime_ns
        self.assertIn("2000-01-01T00:00:00Z", first)
        self.assertEqual(self.generate(), first)
        self.assertEqual(self.output.stat().st_mtime_ns, modification_time)
        with (self.source / "src/plugins/upscale/upscale.cpp").open("a") as stream:
            stream.write("\n// changed\n")
        self.assertNotIn("2000-01-01T00:00:00Z", self.generate())

    def test_reproducible_date(self) -> None:
        """SOURCE_DATE_EPOCH overrides any previously cached build date."""
        self.generate()
        self.environment["SOURCE_DATE_EPOCH"] = "946684800"
        self.assertIn("2000-01-01T00:00:00Z", self.generate())

    def test_quoted_branch(self) -> None:
        """A valid Git branch containing a quote produces a C++ string escape."""
        self.environment["GITHUB_REF_NAME"] = 'topic/"quoted"'
        self.assertIn(r"topic/\"quoted\"", self.generate())
