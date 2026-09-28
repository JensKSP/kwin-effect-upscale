# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Describe a release in SPDX 2.3: its files, what they carry and what they need.

The document is written beside the release files and before the checksums, so
the manifest and the attestation cover it like every other file. What it says
is read from the files themselves and from the repository's own inventory,
never from a list kept here:

- every release file, with its SHA-256, as generated from the source commit,
  and each stable download name as a copy of the package it names;
- the third-party code compiled into the packages, which is every paragraph of
  debian/copyright for files under src/, as contained in the source and in
  every installable package;
- each Debian package's runtime dependencies from its own Depends field, and
  each Debian source package's build and test dependencies from its
  Build-Depends.

The other formats' dependencies are not read. The document says NOASSERTION
for them, which SPDX defines as "not known", rather than saying nothing, which
a reader could take for "none".
"""

from __future__ import annotations

import datetime
import hashlib
import json
import re
import subprocess
from dataclasses import dataclass
from typing import TYPE_CHECKING

import ci_targets

if TYPE_CHECKING:
    from pathlib import Path

PROJECT = "kwin-effect-upscale"
NOASSERTION = "NOASSERTION"
# Debian's name grammar, which stops before an architecture qualifier.
PACKAGE_NAME = re.compile(r"[a-z0-9][a-z0-9+.-]+")


@dataclass(frozen=True)
class Origin:
    """Where a release comes from: the facts no release file states itself."""

    repository: str
    tag: str
    commit: str
    created: datetime.datetime
    # The checkout the release was built from, for debian/copyright.
    source: Path


@dataclass(frozen=True)
class Paragraph:
    """One Files paragraph of debian/copyright."""

    files: tuple[str, ...]
    copyright: str
    license: str
    comment: str


def field(text: str, name: str) -> str:
    """Read one field of a deb822 paragraph, its continuation lines joined."""
    match = re.search(rf"^{name}:(.*(?:\n[ \t].*)*)", text, re.MULTILINE)
    return " ".join(match[1].split()) if match else ""


def paragraphs(copyright_file: Path) -> list[Paragraph]:
    """Read the Files paragraphs of a DEP-5 copyright file."""
    found = []
    for text in re.split(r"\n\s*\n", copyright_file.read_text()):
        files = field(text, "Files")
        if files:
            found.append(
                Paragraph(
                    tuple(files.split()),
                    field(text, "Copyright"),
                    field(text, "License"),
                    field(text, "Comment"),
                )
            )
    return found


def conjunction(licenses: list[str]) -> str:
    """Join licences into one SPDX expression, each once, in the order met."""
    return " AND ".join(dict.fromkeys(licenses))


def identifier(kind: str, name: str) -> str:
    """Make an SPDX identifier from a name, in the characters SPDX allows."""
    return f"SPDXRef-{kind}-" + re.sub(r"[^A-Za-z0-9.-]", "-", name)


def kind(name: str) -> str:
    """Say what a release file is, from its name."""
    if any(part in name for part in ("-dbgsym_", "-debuginfo-", "-debugsource-", "-debug-")):
        return "debug"
    if name.endswith((".buildinfo", ".changes")):
        return "record"
    if name.endswith(".dsc"):
        return "recipe"
    if name.endswith((".tar.gz", ".tar.xz", ".src.rpm")):
        return "source"
    return "binary"


PURPOSES = {
    "binary": "INSTALL",
    "debug": "FILE",
    "record": "FILE",
    "recipe": "SOURCE",
    "source": "SOURCE",
}


def relations(value: str) -> list[tuple[str, str, bool]]:
    """Split a Debian relationship field into name, clause and whether test-only.

    Of alternatives, the first names the dependency, and the whole clause goes
    into the relationship's comment, so nothing of it is lost.
    """
    found = []
    for part in value.split(","):
        clause = " ".join(part.split())
        match = PACKAGE_NAME.match(clause)
        if match is not None:
            found.append((match[0], clause, "<!nocheck>" in clause))
    return found


def deb_field(path: Path, name: str) -> str:
    """Read one control field of a binary Debian package."""
    return subprocess.check_output(["dpkg-deb", "-f", str(path), name], text=True).strip()


def relationship(element: str, relation: str, related: str, comment: str = "") -> dict[str, str]:
    """Say how two elements of the document relate."""
    entry = {"spdxElementId": element, "relationshipType": relation, "relatedSpdxElement": related}
    if comment:
        entry["comment"] = comment
    return entry


class Document:
    """An SPDX document under construction."""

    def __init__(self, version: str, origin: Origin, aliases: set[str]) -> None:
        """Start with the source commit and the third-party code it contains."""
        self.version = version
        self.origin = origin
        self.aliases = aliases
        self.packages: list[dict[str, object]] = []
        self.relationships: list[dict[str, str]] = []
        inventory = paragraphs(origin.source / "debian" / "copyright")
        own = next(entry for entry in inventory if entry.files == ("*",))
        bundled = [
            entry for entry in inventory if all(name.startswith("src/") for name in entry.files)
        ]
        self.binary_license = conjunction([own.license] + [entry.license for entry in bundled])
        self.source_license = conjunction([entry.license for entry in inventory])
        owner, _, project = origin.repository.partition("/")
        self.add(
            "SPDXRef-Source",
            PROJECT,
            versionInfo=version,
            downloadLocation=f"git+https://github.com/{origin.repository}@{origin.commit}",
            homepage=f"https://github.com/{origin.repository}",
            licenseConcluded=self.source_license,
            licenseDeclared=own.license,
            copyrightText=own.copyright,
            primaryPackagePurpose="SOURCE",
            externalRefs=[
                {
                    "referenceCategory": "PACKAGE-MANAGER",
                    "referenceType": "purl",
                    "referenceLocator": f"pkg:github/{owner}/{project}@{origin.commit}",
                }
            ],
        )
        self.relate("SPDXRef-DOCUMENT", "DESCRIBES", "SPDXRef-Source")
        self.bundled = []
        for index, entry in enumerate(bundled, 1):
            # "Name, version, what was done to it." in the paragraph's comment.
            parts = [part.strip() for part in entry.comment.split(",")]
            if len(parts) < 2:  # noqa: PLR2004 - A name and a version.
                message = f"No component name and version in the comment on {entry.files}"
                raise ValueError(message)
            component = f"SPDXRef-Component-{index}"
            self.add(
                component,
                parts[0],
                versionInfo=parts[1],
                supplier="Organization: " + re.sub(r"^[0-9, -]+", "", entry.copyright),
                downloadLocation=NOASSERTION,
                licenseConcluded=entry.license,
                licenseDeclared=entry.license,
                copyrightText=entry.copyright,
                primaryPackagePurpose="SOURCE",
                comment=f"{entry.comment} Files: {' '.join(entry.files)}",
            )
            self.relate("SPDXRef-Source", "CONTAINS", component)
            self.bundled.append(component)

    def add(self, spdx: str, name: str, **fields: object) -> None:
        """Add one package, refusing an identifier that is already taken."""
        if any(package["SPDXID"] == spdx for package in self.packages):
            message = f"Two elements would share the identifier {spdx}"
            raise ValueError(message)
        self.packages.append({"SPDXID": spdx, "name": name, "filesAnalyzed": False, **fields})

    def relate(self, element: str, relation: str, related: str, comment: str = "") -> None:
        """Record one relationship."""
        self.relationships.append(relationship(element, relation, related, comment))

    def dependency(self, distribution: str, clause: tuple[str, str, bool]) -> str:
        """Name a Debian dependency once per distribution, adding it when new."""
        name = clause[0]
        spdx = identifier(f"Debian-{distribution}", name)
        if not any(package["SPDXID"] == spdx for package in self.packages):
            target = ci_targets.target(distribution)
            vendor = target.image.rsplit("/", 1)[-1].split(":")[0]
            # No version: which one is installed is the system's choice, within
            # the clause the relationship's comment quotes.
            self.add(
                spdx,
                name,
                downloadLocation=NOASSERTION,
                comment=f"A {target.label} package.",
                externalRefs=[
                    {
                        "referenceCategory": "PACKAGE-MANAGER",
                        "referenceType": "purl",
                        "referenceLocator": f"pkg:deb/{vendor}/{name}?distro={distribution}",
                    }
                ],
            )
        return spdx

    def release_file(self, path: Path, copies: dict[str, str]) -> None:
        """Describe one release file and what it relates to."""
        name = path.name
        spdx = identifier("File", name)
        with path.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        role = kind(name)
        licence = {
            "binary": self.binary_license,
            "debug": self.binary_license,
            "source": self.source_license,
        }.get(role, NOASSERTION)
        self.add(
            spdx,
            name,
            versionInfo=self.version,
            packageFileName=name,
            downloadLocation=(
                f"https://github.com/{self.origin.repository}/releases/download/"
                f"{self.origin.tag}/{name}"
            ),
            checksums=[{"algorithm": "SHA256", "checksumValue": checksum}],
            licenseConcluded=licence,
            primaryPackagePurpose=PURPOSES[role],
        )
        self.relate("SPDXRef-DOCUMENT", "DESCRIBES", spdx)
        if name in self.aliases:
            if checksum not in copies:
                message = f"{name} is a stable download name that copies no package"
                raise ValueError(message)
            self.relate(spdx, "COPY_OF", identifier("File", copies[checksum]))
            return
        self.relate(spdx, "GENERATED_FROM", "SPDXRef-Source")
        if role in ("binary", "source"):
            for component in self.bundled:
                self.relate(spdx, "CONTAINS", component)
        if role in ("binary", "recipe") and name.endswith((".deb", ".dsc")):
            self.debian_dependencies(path, spdx, role)
        elif role == "binary" or name.endswith((".src.rpm", ".src.tar.gz")):
            # Those formats state dependencies too; they are not read here.
            self.relate(spdx, "DEPENDS_ON", NOASSERTION, "Not read from this package format.")

    def debian_dependencies(self, path: Path, spdx: str, role: str) -> None:
        """Relate a Debian package to what it depends on, as it says itself."""
        if role == "recipe":
            text = path.read_text()
            distribution = field(text, "Version").rpartition("~")[2]
            for clause in relations(field(text, "Build-Depends")):
                relation = "TEST_DEPENDENCY_OF" if clause[2] else "BUILD_DEPENDENCY_OF"
                self.relate(self.dependency(distribution, clause), relation, spdx, clause[1])
            return
        distribution = deb_field(path, "Version").rpartition("~")[2]
        for clause in relations(deb_field(path, "Depends")):
            dependency = self.dependency(distribution, clause)
            self.relate(dependency, "RUNTIME_DEPENDENCY_OF", spdx, clause[1])

    def spdx(self) -> dict[str, object]:
        """Return the document in SPDX's JSON form."""
        return {
            "spdxVersion": "SPDX-2.3",
            "dataLicense": "CC0-1.0",
            "SPDXID": "SPDXRef-DOCUMENT",
            "name": f"{PROJECT} {self.version}",
            "documentNamespace": (
                f"https://github.com/{self.origin.repository}/releases/{self.origin.tag}"
                f"/spdx/{self.origin.commit}"
            ),
            "creationInfo": {
                "created": self.origin.created.astimezone(datetime.UTC).strftime(
                    "%Y-%m-%dT%H:%M:%SZ"
                ),
                "creators": [f"Tool: {PROJECT}-release-tools-{self.version}"],
            },
            "packages": self.packages,
            "relationships": self.relationships,
        }


def sbom_name(version: str) -> str:
    """Name the document among the release files."""
    return f"{PROJECT}-{version}.spdx.json"


def write_sbom(directory: Path, version: str, paths: list[Path], origin: Origin) -> Path:
    """Describe the release files in one SPDX document written beside them."""
    # A stable download name is a byte-for-byte copy of the package it names,
    # which is found by its checksum among the names that are not stable ones.
    aliases = {
        ci_targets.download_name(entry.identifier, architecture)
        for entry in ci_targets.TARGETS
        for architecture in entry.architectures
    }
    copies: dict[str, str] = {}
    for path in sorted(paths):
        if path.name not in aliases:
            with path.open("rb") as stream:
                copies.setdefault(hashlib.file_digest(stream, "sha256").hexdigest(), path.name)
    document = Document(version, origin, aliases)
    for path in sorted(paths):
        document.release_file(path, copies)
    target = directory / sbom_name(version)
    target.write_text(json.dumps(document.spdx(), indent=2) + "\n")
    return target
