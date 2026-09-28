# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a release's SPDX document says what its files are and need."""

import datetime
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Any

from release_sbom import Origin, write_sbom

ROOT = Path(__file__).resolve().parent.parent
VERSION = "0.1.0"
DEB = f"kwin-effect-upscale_{VERSION}.trixie_amd64.deb"
DBGSYM = f"kwin-effect-upscale-dbgsym_{VERSION}.trixie_amd64.deb"
DSC = f"kwin-effect-upscale_{VERSION}.trixie.dsc"
RPM = f"kwin-effect-upscale-{VERSION}-1.fc43.x86_64.rpm"
SRPM = f"kwin-effect-upscale-{VERSION}-1.fc43.src.rpm"
TARBALL = f"kwin-effect-upscale-{VERSION}.tar.gz"
CHANGES = f"kwin-effect-upscale_{VERSION}.trixie_amd64.changes"
ALIAS = "kwin-effect-upscale-debian-amd64.deb"
SHADERS = "AMD FidelityFX Super Resolution 1"
# A document or one of its packages, as JSON decodes it.
Spdx = dict[str, Any]


class ReleaseSbomTest(unittest.TestCase):
    """Every file described once, its contents and dependencies from itself."""

    def setUp(self) -> None:
        """Lay out a small release: real Debian packages beside named stand-ins."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.deb(DEB, "kwin-effect-upscale", "libc6 (>= 2.34), kwin-common (= 4:6.3.6-1)")
        self.deb(DBGSYM, "kwin-effect-upscale-dbgsym", f"kwin-effect-upscale (= {VERSION}~trixie)")
        (self.assets / DSC).write_text(
            f"Format: 3.0 (quilt)\nSource: kwin-effect-upscale\nVersion: {VERSION}~trixie\n"
            "Build-Depends: debhelper-compat (= 13),\n dbus-daemon <!nocheck>,\n"
            " qt6-base-dev (>= 6.8.0) | qt6-base-dev-alternative\n"
        )
        for name in (RPM, SRPM, TARBALL, CHANGES):
            (self.assets / name).write_text(name)
        shutil.copy2(self.assets / DEB, self.assets / ALIAS)
        self.origin = Origin(
            repository="owner/kwin-effect-upscale",
            tag=f"v{VERSION}",
            commit="0123456789abcdef0123456789abcdef01234567",
            created=datetime.datetime(2026, 9, 28, 12, tzinfo=datetime.UTC),
            source=ROOT,
        )

    def deb(self, name: str, package: str, depends: str) -> None:
        """Build a minimal Debian package that states its dependencies."""
        tree = self.root / package
        (tree / "DEBIAN").mkdir(parents=True)
        (tree / "DEBIAN/control").write_text(
            f"Package: {package}\nVersion: {VERSION}~trixie\nArchitecture: amd64\n"
            f"Depends: {depends}\nMaintainer: Test <test@example.org>\nDescription: Fixture\n"
        )
        subprocess.run(
            ["dpkg-deb", "--build", "--root-owner-group", str(tree), str(self.assets / name)],
            check=True,
            capture_output=True,
        )

    def document(self) -> Spdx:
        """Describe the fixture release and read the document back."""
        paths = sorted(self.assets.iterdir())
        written = write_sbom(self.root, VERSION, paths, self.origin)
        self.assertEqual(written.name, f"kwin-effect-upscale-{VERSION}.spdx.json")
        document: Spdx = json.loads(written.read_text())
        return document

    @staticmethod
    def relations(document: Spdx) -> set[tuple[str, str, str]]:
        """Return each relationship as names rather than identifiers."""
        names = {package["SPDXID"]: package["name"] for package in document["packages"]}
        return {
            (
                names.get(entry["spdxElementId"], entry["spdxElementId"]),
                entry["relationshipType"],
                names.get(entry["relatedSpdxElement"], entry["relatedSpdxElement"]),
            )
            for entry in document["relationships"]
        }

    def package(self, document: Spdx, name: str) -> Spdx:
        """Find the one package of a name."""
        found = [package for package in document["packages"] if package["name"] == name]
        self.assertEqual(len(found), 1, name)
        package: Spdx = found[0]
        return package

    def test_every_file_once_with_its_checksum(self) -> None:
        """Each release file is described once, by its SHA-256 and download URL."""
        document = self.document()
        for path in self.assets.iterdir():
            with self.subTest(file=path.name):
                package = self.package(document, path.name)
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(
                    package["checksums"], [{"algorithm": "SHA256", "checksumValue": digest}]
                )
                self.assertEqual(
                    package["downloadLocation"],
                    f"https://github.com/owner/kwin-effect-upscale/releases/download/v{VERSION}/"
                    + path.name,
                )
                self.assertIn(
                    ("SPDXRef-DOCUMENT", "DESCRIBES", path.name), self.relations(document)
                )

    def test_bundled_shaders_come_from_the_copyright_file(self) -> None:
        """AMD's shaders are contained in the source and in what installs, nowhere else."""
        document = self.document()
        shaders = self.package(document, SHADERS)
        self.assertEqual(shaders["versionInfo"], "v1.20210629")
        self.assertEqual(shaders["licenseConcluded"], "MIT")
        self.assertEqual(shaders["supplier"], "Organization: Advanced Micro Devices, Inc.")
        containing = {
            element
            for element, kind, related in self.relations(document)
            if kind == "CONTAINS" and related == SHADERS
        }
        self.assertEqual(containing, {"kwin-effect-upscale", DEB, RPM, SRPM, TARBALL})

    def test_runtime_dependencies_are_read_from_the_package(self) -> None:
        """A Debian package's Depends become its runtime dependencies, clause and all."""
        document = self.document()
        runtime = {
            (entry["spdxElementId"], entry.get("comment"))
            for entry in document["relationships"]
            if entry["relationshipType"] == "RUNTIME_DEPENDENCY_OF"
            and entry["relatedSpdxElement"] == self.package(document, DEB)["SPDXID"]
        }
        self.assertEqual(
            runtime,
            {
                ("SPDXRef-Debian-trixie-libc6", "libc6 (>= 2.34)"),
                ("SPDXRef-Debian-trixie-kwin-common", "kwin-common (= 4:6.3.6-1)"),
            },
        )
        libc = self.package(document, "libc6")
        self.assertEqual(
            libc["externalRefs"][0]["referenceLocator"], "pkg:deb/debian/libc6?distro=trixie"
        )
        self.assertNotIn(
            DBGSYM, {related for _, kind, related in self.relations(document) if "DEPEND" in kind}
        )

    def test_build_and_test_dependencies_are_told_apart(self) -> None:
        """Build-Depends marked <!nocheck> are needed by the tests alone."""
        relations = self.relations(document := self.document())
        self.assertIn(("debhelper-compat", "BUILD_DEPENDENCY_OF", DSC), relations)
        self.assertIn(("dbus-daemon", "TEST_DEPENDENCY_OF", DSC), relations)
        self.assertIn(("qt6-base-dev", "BUILD_DEPENDENCY_OF", DSC), relations)
        clauses = {entry.get("comment") for entry in document["relationships"]}
        self.assertIn("qt6-base-dev (>= 6.8.0) | qt6-base-dev-alternative", clauses)

    def test_unread_formats_say_they_were_not_read(self) -> None:
        """Dependencies not read are NOASSERTION, never simply absent."""
        relations = self.relations(self.document())
        for name in (RPM, SRPM):
            with self.subTest(file=name):
                self.assertIn((name, "DEPENDS_ON", "NOASSERTION"), relations)
        for name in (TARBALL, CHANGES, DBGSYM):
            with self.subTest(file=name):
                self.assertNotIn((name, "DEPENDS_ON", "NOASSERTION"), relations)

    def test_a_stable_name_is_a_copy(self) -> None:
        """A stable download name copies its package and claims nothing of its own."""
        relations = self.relations(self.document())
        self.assertIn((ALIAS, "COPY_OF", DEB), relations)
        self.assertEqual({kind for element, kind, _ in relations if element == ALIAS}, {"COPY_OF"})

    def test_licences(self) -> None:
        """What installs carries the effect's and AMD's licences; the source all three."""
        document = self.document()
        self.assertEqual(
            self.package(document, DEB)["licenseConcluded"], "GPL-2.0-or-later AND MIT"
        )
        self.assertEqual(
            self.package(document, TARBALL)["licenseConcluded"],
            "GPL-2.0-or-later AND MIT AND CC0-1.0",
        )
        self.assertEqual(self.package(document, CHANGES)["licenseConcluded"], "NOASSERTION")

    def test_every_reference_resolves(self) -> None:
        """Identifiers are unique, well formed, and every relationship names one."""
        document = self.document()
        identifiers = [package["SPDXID"] for package in document["packages"]]
        self.assertEqual(len(identifiers), len(set(identifiers)))
        for spdx in identifiers:
            self.assertRegex(spdx, r"^SPDXRef-[A-Za-z0-9.-]+$")
        known = {*identifiers, "SPDXRef-DOCUMENT", "NOASSERTION"}
        for entry in document["relationships"]:
            self.assertIn(entry["spdxElementId"], known)
            self.assertIn(entry["relatedSpdxElement"], known)
        self.assertEqual(document["spdxVersion"], "SPDX-2.3")
        self.assertEqual(document["dataLicense"], "CC0-1.0")
        self.assertEqual(document["creationInfo"]["created"], "2026-09-28T12:00:00Z")

    def test_the_same_release_is_described_the_same(self) -> None:
        """Preparing a release twice writes the same bytes."""
        first = json.dumps(self.document())
        self.assertEqual(json.dumps(self.document()), first)

    def test_bundled_code_needs_a_name_and_version(self) -> None:
        """A paragraph for compiled-in code that names no component is refused."""
        source = self.root / "source"
        (source / "debian").mkdir(parents=True)
        (source / "debian" / "copyright").write_text(
            re.sub(
                r"Comment: AMD[^\n]*\n",
                "",
                (ROOT / "debian" / "copyright").read_text(),
            )
        )
        self.origin = Origin(**{**self.origin.__dict__, "source": source})
        with self.assertRaisesRegex(ValueError, "No component name and version"):
            self.document()


if __name__ == "__main__":
    unittest.main()
