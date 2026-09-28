# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check version identity using real Git and the build-time CMake generator."""

import os
import re
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
            "GITHUB_REF_TYPE",
            "CI_COMMIT_REF_NAME",
            "CI_COMMIT_TAG",
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

    @staticmethod
    def field(generated: str, name: str) -> str:
        """Read the value one generated accessor returns."""
        found = re.search(name + r"\(\)\n\{\n    return (.*?);\n\}", generated)
        if found is None:
            message = f"no accessor {name}() in the generated source"
            raise AssertionError(message)
        literal = re.fullmatch(r'QStringLiteral\("(.*)"\)', found.group(1))
        return literal.group(1) if literal else found.group(1)

    def test_commit_branch_and_tag_apart(self) -> None:
        """The full commit, the branch and a tag on it are separate fields."""
        generated = self.generate()
        self.assertEqual(self.field(generated, "commit"), self.git("rev-parse", "HEAD"))
        branch = self.git("rev-parse", "--abbrev-ref", "HEAD")
        self.assertEqual(self.field(generated, "branch"), branch)
        self.assertEqual(self.field(generated, "tag"), "")
        self.git("tag", "v0.1.0")
        generated = self.generate()
        self.assertEqual(self.field(generated, "branch"), branch)
        self.assertEqual(self.field(generated, "tag"), "v0.1.0")

    def test_forge_tag_is_no_branch(self) -> None:
        """A forge's build for a tag names the tag, and no branch."""
        self.environment["GITHUB_REF_NAME"] = "v0.1.0"
        self.environment["GITHUB_REF_TYPE"] = "tag"
        generated = self.generate()
        self.assertEqual(self.field(generated, "tag"), "v0.1.0")
        self.assertEqual(self.field(generated, "branch"), "")

    def test_reproducible_date_is_said(self) -> None:
        """A date from SOURCE_DATE_EPOCH is marked as one."""
        self.assertEqual(self.field(self.generate(), "reproducibleBuildDate"), "false")
        self.environment["SOURCE_DATE_EPOCH"] = "946684800"
        self.assertEqual(self.field(self.generate(), "reproducibleBuildDate"), "true")

    def test_tags(self) -> None:
        """Only the matching release tag suppresses the Git suffix."""
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "nightly")
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "v9.9.9")
        self.assertIn("0.1.0+git", self.generate())
        self.git("tag", "v0.1.0")
        generated = self.generate()
        self.assertIn('QStringLiteral("0.1.0")', generated)
        revision = self.git("rev-parse", "--short=10", "HEAD")
        self.assertIn(f'QStringLiteral("{revision}")', generated)
        with (self.source / "README.md").open("a") as stream:
            stream.write("\nChanged source.\n")
        self.assertIn("-dirty", self.generate())

    def test_detached_tag(self) -> None:
        """A detached release checkout reports its tag, and no branch."""
        self.git("tag", "v0.1.0")
        self.git("checkout", "--detach", "v0.1.0")
        generated = self.generate()
        self.assertEqual(self.field(generated, "tag"), "v0.1.0")
        self.assertEqual(self.field(generated, "branch"), "")

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
        commit = "0123456789" + "a" * 30
        metadata.write_text(
            "# archive metadata\n0.1.0+git20260917.0123456789\n"
            f"commit={commit}\nbranch=release/0.1.0\ntag=v0.1.0\n"
        )
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
        generated = self.generate()
        self.assertIn('QStringLiteral("0.1.0+git20260917.0123456789")', generated)
        self.assertIn('QStringLiteral("0123456789")', generated)
        self.assertEqual(self.field(generated, "commit"), commit)
        self.assertEqual(self.field(generated, "branch"), "release/0.1.0")
        self.assertEqual(self.field(generated, "tag"), "v0.1.0")

    def test_unchanged_build(self) -> None:
        """Reproducible builds retain the generated source mtime when unchanged."""
        self.environment["SOURCE_DATE_EPOCH"] = "946684800"
        first = self.generate()
        modification_time = self.output.stat().st_mtime_ns
        self.assertIn("2000-01-01T00:00:00Z", first)
        self.assertEqual(self.generate(), first)
        self.assertEqual(self.output.stat().st_mtime_ns, modification_time)

    def test_build_date_refreshes(self) -> None:
        """A later build invocation records a new date without source changes.

        Each invocation reads the clock, through the same string(TIMESTAMP)
        that SOURCE_DATE_EPOCH overrides; two epochs stand in for two readings
        rather than a wait for the clock's second to turn.
        """
        self.environment["SOURCE_DATE_EPOCH"] = "946684800"
        first = self.generate()
        self.environment["SOURCE_DATE_EPOCH"] = "946684801"
        self.assertNotEqual(self.generate(), first)

    def test_reproducible_date(self) -> None:
        """SOURCE_DATE_EPOCH supplies the requested UTC timestamp."""
        self.generate()
        self.environment["SOURCE_DATE_EPOCH"] = "946684800"
        self.assertIn("2000-01-01T00:00:00Z", self.generate())

    def test_quoted_branch(self) -> None:
        """A valid Git branch containing a quote produces a C++ string escape."""
        self.environment["GITHUB_REF_NAME"] = 'topic/"quoted"'
        self.assertIn(r"topic/\"quoted\"", self.generate())
