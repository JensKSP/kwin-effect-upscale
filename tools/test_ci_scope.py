# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ensure documentation gating cannot hide mixed, renamed or unknown inputs."""

import contextlib
import io
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from ci_scope import changed_files, main, scope

ROOT = Path(__file__).resolve().parent.parent


class ScopeTest(unittest.TestCase):
    """Only positively identified documentation PRs may skip build jobs."""

    def test_documentation_only(self) -> None:
        """Recognize text without granting a blanket exemption to directories."""
        selected = scope(
            ["README.md", "AGENTS.md", "doc/agents/slice.md", ".github/PULL_REQUEST_TEMPLATE.md"],
            pull_request=True,
        )
        self.assertEqual(selected["build"], "false")
        self.assertEqual(selected["packaging"], "false")
        self.assertEqual(json.loads(selected["modes"]), ["docs"])

    def test_build_boundaries(self) -> None:
        """Code, CI settings and unknown paths still select the complete checks."""
        for path in (
            "src/plugins/upscale/effect.cpp",
            "doc/example.cpp",
            "README.md.in",
            "CMakeLists.txt",
            "tools/check.py",
            ".pre-commit-config.yaml",
            ".github/workflows/ci.yml",
            "new-input.unknown",
        ):
            with self.subTest(path=path):
                selected = scope(["README.md", path], pull_request=True)
                self.assertEqual(selected["build"], "true")
                self.assertEqual(json.loads(selected["modes"]), ["lint", "gcc", "clang", "tidy"])
        self.assertEqual(scope([], pull_request=True)["build"], "true")

    def test_publication_and_manual_are_full(self) -> None:
        """Nightly, release and manual runs retain complete validation."""
        selected = scope(["README.md"], pull_request=False)
        self.assertEqual(selected["build"], "true")
        self.assertEqual(selected["packaging"], "false")

    def test_documentation_master_push(self) -> None:
        """A documentation-only merge does not repeat the expensive jobs."""
        selected = scope(["README.md"], pull_request=False, master_push=True)
        self.assertEqual(selected["build"], "false")
        self.assertEqual(selected["packaging"], "false")

    def test_unknown_comparison_is_full(self) -> None:
        """A missing base or Git failure must also retain package validation."""
        for error in (ValueError(), subprocess.CalledProcessError(1, "git")):
            output = io.StringIO()
            with (
                patch.dict(os.environ, {"GITHUB_EVENT_NAME": "pull_request"}),
                patch("ci_scope.changed_files", side_effect=error),
                contextlib.redirect_stdout(output),
            ):
                main()
            self.assertIn("build=true\n", output.getvalue())
            self.assertIn("packaging=true\n", output.getvalue())

    def test_real_rename_and_deletion(self) -> None:
        """A source renamed into documentation or deleted still requires builds."""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            subprocess.run(["git", "clone", "--quiet", str(ROOT), str(source)], check=True)
            script = source / "tools/quality_gate.py"
            script.rename(source / "doc/renamed.md")
            (source / "tools/review_approval.py").unlink()
            (source / "doc/with\nnewline.md").write_text("Text\n")
            subprocess.run(["git", "-C", str(source), "add", "-A"], check=True)
            # Trees need no test commits or artificial author identity.
            tree = subprocess.check_output(
                ["git", "-C", str(source), "write-tree"], text=True
            ).strip()
            with contextlib.chdir(source):
                paths = changed_files("HEAD", tree)
            self.assertIn("tools/quality_gate.py", paths)
            self.assertIn("tools/review_approval.py", paths)
            self.assertIn("doc/with\nnewline.md", paths)
            self.assertEqual(scope(paths, pull_request=True)["build"], "true")
