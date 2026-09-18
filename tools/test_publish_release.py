# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise publication failure boundaries without contacting GitHub."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PUBLISH = Path(__file__).with_name("publish-release.py").resolve()


class PublicationTest(unittest.TestCase):
    """A failed replacement must leave the existing nightly available."""

    def setUp(self) -> None:
        """Provide a local gh stand-in that simulates uploads and downloads."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "build").mkdir()
        self.assets = self.root / "assets"
        self.assets.mkdir()
        (self.assets / "package.deb").write_bytes(b"validated-package")
        binary = self.root / "gh"
        binary.write_text(
            f"#!{sys.executable}\n"
            """import json
import os
from pathlib import Path
import shutil
import sys

args = sys.argv[1:]
with Path("calls.jsonl").open("a") as stream:
    stream.write(json.dumps(args) + "\\n")
mode = os.environ["TEST_FAILURE"]
if args[0] == "api":
    if mode == "api":
        sys.exit(1)
    print(json.dumps(["nightly", False]))
    print(json.dumps(["v0.1.0", False]))
elif args[1] == "upload" and mode == "upload":
    sys.exit(1)
elif args[1] == "download":
    destination = Path(args[args.index("--dir") + 1])
    for source in Path("assets").iterdir():
        shutil.copy2(source, destination / source.name)
    if mode == "corrupt":
        (destination / "package.deb").write_bytes(b"corrupted")
"""
        )
        binary.chmod(0o755)

    def publish(
        self, failure: str = "", channel: str = "nightly"
    ) -> subprocess.CompletedProcess[str]:
        """Execute the real publication entry point against the simulated service."""
        return subprocess.run(
            [sys.executable, str(PUBLISH), "0.1.0", channel, "assets"],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
            env={
                **os.environ,
                "PATH": str(self.root) + os.pathsep + os.environ["PATH"],
                "GITHUB_REPOSITORY": "example/project",
                "GITHUB_SHA": "a" * 40,
                "GITHUB_RUN_ID": "42",
                "TEST_FAILURE": failure,
            },
        )

    def calls(self) -> list[list[str]]:
        """Read the service operations issued by the publication script."""
        return [json.loads(line) for line in (self.root / "calls.jsonl").read_text().splitlines()]

    def test_failure_preserves_previous_nightly(self) -> None:
        """API, upload and verification errors never delete a published release."""
        for failure in ("api", "upload", "corrupt"):
            with self.subTest(failure=failure):
                (self.root / "calls.jsonl").unlink(missing_ok=True)
                result = self.publish(failure)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(any(call[:2] == ["release", "delete"] for call in self.calls()))
                self.assertFalse(any(call[:2] == ["release", "edit"] for call in self.calls()))

    def test_success_checks_upload_before_replacing(self) -> None:
        """The final tag moves only after replacement assets have been downloaded."""
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        operations = [call[1] for call in self.calls() if call[0] == "release"]
        self.assertEqual(operations, ["create", "upload", "download", "delete", "edit"])

    def test_stable_release_is_never_overwritten(self) -> None:
        """Re-running publication validates the existing release without mutating it."""
        result = self.publish(channel="release")
        self.assertEqual(result.returncode, 0, result.stderr)
        operations = [call[1] for call in self.calls() if call[0] == "release"]
        self.assertEqual(operations, ["download"])
