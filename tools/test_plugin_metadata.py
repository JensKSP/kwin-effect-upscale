# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the KPluginMetaData schema check."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from git_fixture import detach

CHECKER = Path(__file__).resolve().with_name("check-plugin-metadata.py")


def setUpModule() -> None:
    """Keep fixture repositories out of the repository this check is running for."""
    detach()


# A stand-in for check-jsonschema: it records the files it was asked to
# validate, so a test can assert which ones the checker selected.
STUB = """#!/usr/bin/env python3
import os, sys
with open(os.environ["STUB_LOG"], "w", encoding="utf-8") as handle:
    handle.write("\\n".join(sys.argv[1:]))
sys.exit(int(os.environ.get("STUB_STATUS", "0")))
"""

METADATA = {"KPlugin": {"Id": "upscale", "Name": "Upscale"}}


class PluginMetadataTest(unittest.TestCase):
    """Run the checker against a throwaway repository and a stub validator."""

    def setUp(self) -> None:
        """Build a repository, a schema file and a validator on PATH."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

        self.binaries = self.root / "bin"
        self.binaries.mkdir()
        validator = self.binaries / "check-jsonschema"
        validator.write_text(STUB, encoding="utf-8")
        validator.chmod(0o755)
        self.log = self.root / "validated.txt"

        self.schema = self.root / "kpluginmetadata.schema.json"
        self.schema.write_text(json.dumps({"type": "object"}), encoding="utf-8")

        subprocess.run(
            ("git", "-C", str(self.root), "init", "--quiet", "--initial-branch=master"),
            check=True,
        )

    def add(self, name: str, content: str) -> None:
        """Write a file and track it, because the checker asks git for its list."""
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        subprocess.run(("git", "-C", str(self.root), "add", "--", name), check=True)

    def check(self, **overrides: str) -> subprocess.CompletedProcess[str]:
        """Run the checker with the stub ahead of anything else on PATH."""
        environment = dict(os.environ)
        environment["PATH"] = f"{self.binaries}{os.pathsep}{environment['PATH']}"
        environment["KPLUGINMETADATA_SCHEMA"] = str(self.schema)
        environment["STUB_LOG"] = str(self.log)
        environment.update(overrides)
        return subprocess.run(
            [sys.executable, str(CHECKER)],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
            env=environment,
        )

    def test_selects_plugin_metadata(self) -> None:
        """Only JSON with a KPlugin object at its root is plugin metadata."""
        self.add("src/plugins/upscale/metadata.json", json.dumps(METADATA))
        self.add("compile_commands.json", json.dumps([{"file": "upscale.cpp"}]))
        self.add("settings.json", json.dumps({"Other": {"Id": "x"}}))
        result = self.check()
        self.assertEqual(result.returncode, 0)
        self.assertEqual(
            self.log.read_text(encoding="utf-8").splitlines()[-1],
            "src/plugins/upscale/metadata.json",
        )

    def test_reports_validator_failure(self) -> None:
        """The validator's own exit status is what the checker returns."""
        self.add("src/plugins/upscale/metadata.json", json.dumps(METADATA))
        self.assertEqual(self.check(STUB_STATUS="3").returncode, 3)

    def test_missing_metadata(self) -> None:
        """A tree without plugin metadata is an error; this project ships one."""
        self.add("compile_commands.json", json.dumps([]))
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("no KPluginMetaData file found", result.stderr)

    def test_unparsable_json(self) -> None:
        """A JSON file that does not parse is not metadata, and not a crash."""
        self.add("broken.json", "{ not json")
        self.add("src/plugins/upscale/metadata.json", json.dumps(METADATA))
        self.assertEqual(self.check().returncode, 0)

    def test_missing_schema(self) -> None:
        """Without the KDE schema the checker says where it comes from."""
        self.add("src/plugins/upscale/metadata.json", json.dumps(METADATA))
        result = self.check(KPLUGINMETADATA_SCHEMA=str(self.root / "absent.json"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("libkf6coreaddons-data", result.stderr)

    def test_missing_validator(self) -> None:
        """Without check-jsonschema the checker says how to install it."""
        result = self.check(PATH=str(self.root / "empty"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("check-jsonschema is not installed", result.stderr)
