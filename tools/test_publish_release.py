# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise publication failure boundaries without contacting GitHub."""

import json
import os
import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

PUBLISH = Path(__file__).with_name("publish-release.py").resolve()


class PublicationTest(unittest.TestCase):
    """Protect the old nightly until verification, then recover promotion errors."""

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
if args[:3] == ["api", "--method", "PATCH"]:
    state = Path("promotion-attempts")
    count = int(state.read_text()) + 1 if state.exists() else 1
    state.write_text(str(count))
    if mode == "promotion" or (mode == "transient" and count < 3):
        sys.exit(1)
elif args[0] == "api":
    if mode == "api":
        sys.exit(1)
    print(json.dumps(["nightly", False]))
    print(json.dumps(["v0.1.0", False]))
elif args[1] == "upload" and mode == "upload":
    sys.exit(1)
elif args[1] == "view":
    if mode == "view":
        sys.exit(1)
    print("123")
elif args[1] == "download":
    destination = Path(args[args.index("--dir") + 1])
    for source in Path("assets").iterdir():
        shutil.copy2(source, destination / source.name.replace("~", "."))
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
        for failure in ("api", "upload", "corrupt", "view"):
            with self.subTest(failure=failure):
                (self.root / "calls.jsonl").unlink(missing_ok=True)
                result = self.publish(failure)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(any(call[:2] == ["release", "delete"] for call in self.calls()))
                self.assertFalse(any(call[:2] == ["release", "edit"] for call in self.calls()))
                self.assertFalse(any("PATCH" in call for call in self.calls()))

    def test_success_checks_upload_before_replacing(self) -> None:
        """The final tag moves only after replacement assets have been downloaded."""
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        operations = [call[1] for call in self.calls() if call[0] == "release"]
        self.assertEqual(operations, ["create", "upload", "download", "view", "delete"])
        self.assertEqual(
            self.calls()[-1][:4], ["api", "--method", "PATCH", "repos/example/project/releases/123"]
        )

    def test_hosted_rename_keeps_previous_nightly_and_identifies_mismatch(self) -> None:
        """An unprepared Debian filename reproduces GitHub's asset renaming."""
        (self.assets / "package_0.1.0~trixie_amd64.deb").write_bytes(b"package")
        result = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing=['package_0.1.0~trixie_amd64.deb']", result.stderr)
        self.assertIn("unexpected=['package_0.1.0.trixie_amd64.deb']", result.stderr)
        self.assertFalse(any(call[:2] == ["release", "delete"] for call in self.calls()))
        self.assertFalse(any("PATCH" in call for call in self.calls()))

    def test_transient_promotion_failure_recovers(self) -> None:
        """Failure after deletion retries the same release ID without reuploading."""
        result = self.publish("transient")
        self.assertEqual(result.returncode, 0, result.stderr)
        patches = [call for call in self.calls() if "PATCH" in call]
        self.assertEqual(len(patches), 3)
        self.assertTrue(all(call == patches[0] for call in patches))
        self.assertIn("tag_name=nightly", patches[0])
        self.assertIn("draft=false", patches[0])
        self.assertEqual(sum(call[:2] == ["release", "delete"] for call in self.calls()), 1)

    def test_persistent_promotion_failure_reports_recovery(self) -> None:
        """Exhausted retries retain the verified candidate and expose recovery."""
        result = self.publish("promotion")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sum("PATCH" in call for call in self.calls()), 3)
        self.assertIn("retry promotion with: gh api --method PATCH", result.stderr)
        self.assertIn("repos/example/project/releases/123", result.stderr)
        self.assertFalse(
            any(call[:3] == ["release", "delete", "nightly-staging-42"] for call in self.calls())
        )

    def test_stable_release_is_never_overwritten(self) -> None:
        """Re-running publication validates the existing release without mutating it."""
        result = self.publish(channel="release")
        self.assertEqual(result.returncode, 0, result.stderr)
        operations = [call[1] for call in self.calls() if call[0] == "release"]
        self.assertEqual(operations, ["download"])


class InstallationGuideTest(unittest.TestCase):
    """The release page answers "which file do I need" before anything else."""

    version = "0.1.0+git20260920.1482287abc"

    def assets(self) -> list[str]:
        """Name everything one nightly candidate actually carries."""
        version = self.version
        names = [f"kwin-effect-upscale-{version}.tar.gz", "SHA256SUMS", "provenance.sigstore.json"]
        for distribution in ("trixie", "resolute"):
            for architecture in ("amd64", "arm64"):
                stem = f"{version}.{distribution}_{architecture}"
                names += [
                    f"kwin-effect-upscale_{stem}.deb",
                    f"kwin-effect-upscale-dbgsym_{stem}.deb",
                    f"kwin-effect-upscale_{stem}.buildinfo",
                    f"kwin-effect-upscale_{stem}.changes",
                ]
            names += [
                f"kwin-effect-upscale_{version}.{distribution}.dsc",
                f"kwin-effect-upscale_{version}.{distribution}.tar.xz",
            ]
        for architecture in ("x86_64", "aarch64"):
            names += [
                f"kwin-effect-upscale-{version}-1.fc43.{architecture}.rpm",
                f"kwin-effect-upscale-debuginfo-{version}-1.fc43.{architecture}.rpm",
                f"kwin-effect-upscale-{version}-1.{architecture}.rpm",
                f"kwin-effect-upscale-debuginfo-{version}-1.{architecture}.rpm",
            ]
        return [
            *names,
            f"kwin-effect-upscale-{version}-1.fc43.src.rpm",
            f"kwin-effect-upscale-{version}-1.src.rpm",
            f"kwin-effect-upscale-{version}-1-x86_64.pkg.tar.zst",
            f"kwin-effect-upscale-debug-{version}-1-x86_64.pkg.tar.zst",
            f"kwin-effect-upscale-{version}-1.src.tar.gz",
        ]

    def guide(self) -> str:
        """Render the notes the publisher writes, without contacting GitHub."""
        module = runpy.run_path(str(PUBLISH))
        return str(module["installation_guide"]("owner/repo", "nightly", self.assets()))

    def test_debian_trixie_on_a_desktop_is_named_first(self) -> None:
        """It is the one build with an acceptance machine behind it."""
        rows = [line for line in self.guide().splitlines() if line.startswith("| ")]
        self.assertIn("Debian Trixie", rows[2])
        self.assertIn("amd64", rows[2])
        self.assertIn(f"kwin-effect-upscale_{self.version}.trixie_amd64.deb", rows[2])

    def test_every_installable_package_is_offered_once(self) -> None:
        """One row per distribution and architecture, and no companion files."""
        rows = [line for line in self.guide().splitlines() if line.startswith("| ")][2:]
        self.assertEqual(len(rows), 9)
        for row in rows:
            for companion in ("-dbgsym_", "-debuginfo-", "-debug-", ".src.", ".dsc", ".buildinfo"):
                self.assertNotIn(companion, row)

    def test_the_rest_is_folded_away_and_nothing_is_lost(self) -> None:
        """Every asset appears exactly once, in the table or under the fold."""
        guide = self.guide()
        self.assertIn("<details>", guide)
        for name in self.assets():
            self.assertEqual(guide.count(f"/{name})"), 1, name)

    def test_opensuse_is_not_confused_with_fedora(self) -> None:
        """Only openSUSE's names carry no distribution stamp at all."""
        guide = self.guide()
        row = "| openSUSE Tumbleweed | 64-bit PC (x86_64) | "
        self.assertIn(f"{row}[kwin-effect-upscale-{self.version}-1.x86_64.rpm]", guide)
        self.assertNotIn(f"{row}[kwin-effect-upscale-{self.version}-1.fc43", guide)
