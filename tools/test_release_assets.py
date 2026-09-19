# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise release validation with real Debian archives and corrupted inventories."""

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

from release_assets import (
    ARCHITECTURES,
    DISTRIBUTIONS,
    package_changelog,
    validate_assets,
    validate_version,
    write_manifest,
)


class ReleaseAssetsTest(unittest.TestCase):
    """Reject partial, mislabeled and contaminated release downloads."""

    def setUp(self) -> None:
        """Create the four-platform inventory with tiny but valid Debian packages."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.version = "0.1.0"
        for distribution in DISTRIBUTIONS:
            for architecture in ARCHITECTURES:
                version = f"{self.version}~{distribution}"
                suffix = f"{version}_{architecture}"
                for debug in (False, True):
                    name = "kwin-effect-upscale" + ("-dbgsym" if debug else "")
                    package = self.root / f"{name}-{distribution}-{architecture}"
                    (package / "DEBIAN").mkdir(parents=True)
                    (package / "DEBIAN/control").write_text(
                        f"Package: {name}\nVersion: {version}\nArchitecture: {architecture}\n"
                        "Maintainer: Test <test@example.org>\nDescription: Test fixture\n"
                    )
                    extension = "ddeb" if debug and distribution == "resolute" else "deb"
                    subprocess.run(
                        [
                            "dpkg-deb",
                            "--build",
                            "--root-owner-group",
                            str(package),
                            str(self.assets / f"{name}_{suffix}.{extension}"),
                        ],
                        check=True,
                        capture_output=True,
                    )
                for extension in ("changes", "buildinfo"):
                    (self.assets / f"kwin-effect-upscale_{suffix}.{extension}").write_text(
                        "record\n"
                    )
        (self.assets / f"kwin-effect-upscale-{self.version}.tar.gz").write_bytes(b"archive")

    def test_manifest_covers_every_deliverable(self) -> None:
        """Checksums cover binaries, symbols, build records and source."""
        expected = {path.name.replace("~", ".") for path in self.assets.iterdir()}
        write_manifest(self.assets, self.version)
        entries = (self.assets / "SHA256SUMS").read_text().splitlines()
        self.assertEqual({line.split("  ")[1] for line in entries}, expected)
        for line in entries:
            checksum, name = line.split("  ")
            self.assertEqual(
                checksum, hashlib.sha256((self.assets / name).read_bytes()).hexdigest()
            )

    def test_public_names_preserve_package_versions_and_build_records(self) -> None:
        """Hosted filenames must survive upload without changing any payload."""
        original = {
            path.name.replace("~", "."): path.read_bytes() for path in self.assets.iterdir()
        }
        write_manifest(self.assets, self.version)
        for name, contents in original.items():
            self.assertNotIn("~", name)
            self.assertEqual((self.assets / name).read_bytes(), contents)
        package = self.assets / "kwin-effect-upscale_0.1.0.trixie_amd64.deb"
        version = subprocess.check_output(["dpkg-deb", "-f", str(package), "Version"], text=True)
        self.assertEqual(version.strip(), "0.1.0~trixie")

    def test_invalid_inventory_is_not_renamed(self) -> None:
        """Validate everything before mutating names or creating a manifest."""
        (self.assets / "unexpected.deb").write_bytes(b"unexpected")
        original = {path.name for path in self.assets.iterdir()}
        with self.assertRaises(ValueError):
            write_manifest(self.assets, self.version)
        self.assertEqual({path.name for path in self.assets.iterdir()}, original)

    def test_reject_missing_package(self) -> None:
        """A successful subset of the matrix must never publish."""
        next(self.assets.glob("*.deb")).unlink()
        with self.assertRaises(ValueError):
            validate_assets(self.assets, self.version)

    def test_reject_report_directory(self) -> None:
        """Instrumentation reports cannot become release assets."""
        (self.assets / "coverage").mkdir()
        with self.assertRaises(ValueError):
            validate_assets(self.assets, self.version)

    def test_reject_renamed_architecture(self) -> None:
        """Filenames cannot disguise the wrong package metadata."""
        amd = self.assets / "kwin-effect-upscale_0.1.0~trixie_amd64.deb"
        arm = self.assets / "kwin-effect-upscale_0.1.0~trixie_arm64.deb"
        arm.write_bytes(amd.read_bytes())
        with self.assertRaises(ValueError):
            validate_assets(self.assets, self.version)

    def test_reject_symlink(self) -> None:
        """A source asset must contain bytes rather than refer outside the inventory."""
        source = next(self.assets.glob("*.tar.gz"))
        source.unlink()
        source.symlink_to(next(self.assets.glob("*.deb")))
        with self.assertRaises(ValueError):
            validate_assets(self.assets, self.version)

    def test_fixed_changelog_timestamp(self) -> None:
        """Package timestamps use the selected commit's epoch."""
        entry = package_changelog("0.1.0~trixie", "abcdef", 946684800, "Test <test@example.org>")
        self.assertIn("Sat, 01 Jan 2000 00:00:00 +0000", entry)

    def test_invalid_versions(self) -> None:
        """Unvalidated tags cannot be interpolated into paths or package versions."""
        for version in ("../0.1.0", "0.1.0\n", "0.1.0;command", "v0.1.0"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate_version(version)
