# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the two checkers that enforce the AGENTS.md rules.

This file spells out the phrases the checkers look for, so ai_attribution lists
it among the files the content scan skips.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ARTIFACTS = TOOLS / "check-no-ai-artifacts.py"
TRAILERS = TOOLS / "check-commit-trailers.py"

TRAILER = "Co-authored-by: Claude <noreply@anthropic.com>"
MIXED_CASE_TRAILER = "Co-Authored-By: Claude Opus <noreply@anthropic.com>"


class RepositoryTest(unittest.TestCase):
    """Run each checker against a throwaway repository."""

    def setUp(self) -> None:
        """Create a repository with one clean commit to build on."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.git("init", "--quiet", "--initial-branch=master")
        self.git("config", "user.name", "Tester")
        self.git("config", "user.email", "tester@example.com")
        self.commit("README.md", "# Project\n", "docs: Add a readme")

    def git(self, *arguments: str) -> str:
        """Run git inside the throwaway repository."""
        return subprocess.run(
            ("git", "-C", str(self.root), *arguments),
            capture_output=True,
            text=True,
            check=True,
        ).stdout

    def write(self, name: str, content: str | bytes) -> Path:
        """Create a file, and any directory it needs, inside the repository."""
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content.encode() if isinstance(content, str) else content)
        return path

    def stage(self, name: str, content: str | bytes) -> None:
        """Write a file and stage it, which is what the commit hook sees."""
        self.write(name, content)
        self.git("add", "--", name)

    def commit(self, name: str, content: str, message: str) -> None:
        """Add one file and commit it with the given message."""
        self.stage(name, content)
        self.git("commit", "--quiet", "--message", message)

    def check(self, checker: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
        """Run a checker in the repository, exactly as the hook and CI do."""
        return subprocess.run(
            [sys.executable, str(checker), *arguments],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )


class ArtifactTest(RepositoryTest):
    """Exercise the tree and staging scan."""

    def test_clean_tree(self) -> None:
        """A repository with nothing to report passes both modes."""
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)
        self.assertEqual(self.check(ARTIFACTS, "--all").returncode, 0)

    def test_forbidden_paths(self) -> None:
        """Agent tooling is rejected wherever in the tree it sits."""
        for name in (
            "CLAUDE.md",
            ".claude/settings.json",
            "src/.cursorrules",
            ".mcp.json",
            "doc/.aider.chat.history.md",
            "skills/superpowers/README.md",
        ):
            with self.subTest(name=name):
                self.stage(name, "content\n")
                result = self.check(ARTIFACTS)
                self.git("rm", "--quiet", "--cached", "--", name)
                self.assertEqual(result.returncode, 1)
                self.assertIn("agent leftover", result.stderr)

    def test_agents_file_is_allowed(self) -> None:
        """AGENTS.md is the one agent-facing file this project keeps."""
        self.stage("AGENTS.md", "# Rules\n")
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)

    def test_attribution_in_content(self) -> None:
        """A file that says how it was written is rejected."""
        for content in (
            f"// {TRAILER}\n",
            "# Assisted-by: a tool\n",
            "// SPDX-FileCopyrightText: 2026 Claude\n",
            "Generated with [Claude Code](https://claude.com/claude-code)\n",
            "\N{ROBOT FACE} Generated with something\n",
        ):
            with self.subTest(content=content):
                self.stage("source.cpp", content)
                result = self.check(ARTIFACTS)
                self.assertEqual(result.returncode, 1)
                self.assertIn("mentions how it was written", result.stderr)

    def test_trailer_case_is_ignored(self) -> None:
        """The forge spelling of the trailer is the same trailer.

        The shell checker this replaced matched the file contents case
        sensitively and let this exact line through.
        """
        self.stage("source.cpp", f"// {MIXED_CASE_TRAILER}\n")
        self.assertEqual(self.check(ARTIFACTS).returncode, 1)

    def test_staged_only(self) -> None:
        """The commit scan looks at what is staged; the tree scan at all of it."""
        self.write("stray.cpp", f"// {TRAILER}\n")
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)
        self.git("add", "--", "stray.cpp")
        self.git("commit", "--quiet", "--no-verify", "--message", "feat: Add a stray")
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)
        self.assertEqual(self.check(ARTIFACTS, "--all").returncode, 1)

    def test_binary_file(self) -> None:
        """A binary file is skipped rather than decoded and reported."""
        self.stage("blob.bin", b"\x00\x01\x02" + TRAILER.encode() + b"\x00")
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)

    def test_invalid_encoding(self) -> None:
        """One undecodable byte does not hide the rest of a text file."""
        self.stage("source.cpp", b"// \xff\n// " + TRAILER.encode() + b"\n")
        self.assertEqual(self.check(ARTIFACTS).returncode, 1)

    def test_deleted_file(self) -> None:
        """A path that no longer exists is not opened."""
        self.commit("gone.cpp", "int value;\n", "feat: Add a file")
        self.git("rm", "--quiet", "--", "gone.cpp")
        self.assertEqual(self.check(ARTIFACTS).returncode, 0)


class TrailerTest(RepositoryTest):
    """Exercise the commit message scan."""

    def test_clean_commit(self) -> None:
        """An ordinary commit message passes."""
        self.commit("source.cpp", "int value;\n", "feat: Add a source file")
        self.assertEqual(self.check(TRAILERS).returncode, 0)

    def test_attributed_commit(self) -> None:
        """A commit naming a tool as co-author is rejected."""
        for trailer in (TRAILER, MIXED_CASE_TRAILER, "Assisted-by: a tool"):
            with self.subTest(trailer=trailer):
                self.commit("source.cpp", "int value;\n", f"feat: Work\n\n{trailer}")
                result = self.check(TRAILERS)
                self.assertEqual(result.returncode, 1)
                self.assertIn("AI attribution trailer", result.stderr)
                self.git("reset", "--quiet", "--hard", "HEAD~1")

    def test_bot_address(self) -> None:
        """A forge bot is a tool as much as an assistant is."""
        message = "feat: Work\n\nCo-authored-by: A Bot <dependabot@example.com>"
        self.commit("source.cpp", "int value;\n", message)
        self.assertEqual(self.check(TRAILERS).returncode, 1)

    def test_explicit_range(self) -> None:
        """Only the commits in the given range are checked."""
        base = self.git("rev-parse", "HEAD").strip()
        self.commit("one.cpp", "int one;\n", f"feat: One\n\n{TRAILER}")
        self.commit("two.cpp", "int two;\n", "feat: Two")
        self.assertEqual(self.check(TRAILERS, "HEAD~1..HEAD").returncode, 0)
        self.assertEqual(self.check(TRAILERS, f"{base}..HEAD").returncode, 1)

    def test_unresolvable_range(self) -> None:
        """A range CI cannot resolve falls back to the whole history."""
        self.commit("one.cpp", "int one;\n", f"feat: One\n\n{TRAILER}")
        missing = "0" * 40
        result = self.check(TRAILERS, f"{missing}..HEAD")
        self.assertEqual(result.returncode, 1)

    def test_first_commit(self) -> None:
        """The default range works in a repository with a single commit."""
        self.assertEqual(self.check(TRAILERS).returncode, 0)
