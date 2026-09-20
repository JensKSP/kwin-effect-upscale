# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate release contents and create deterministic package metadata."""

import datetime
import email.utils
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

import ci_targets

VERSION_PATTERN = r"[0-9]+\.[0-9]+\.[0-9]+(?:\+git[0-9]{8}\.[0-9a-f]{10})?"
# Which targets exist, and what each is built for, is ci_targets.py's to say.
# This file knows only what their file names look like once built.
DISTRIBUTIONS = ci_targets.DEB_TARGETS
ARCHITECTURES = ci_targets.ARCHITECTURES

# The distributions whose packages are built in the nightly beside the Debian
# ones. Their file names cannot be enumerated the way the Debian matrix can:
# Fedora stamps its release with %{?dist}, so the name carries fc43 and will
# carry fc44, and Arch names the architecture x86_64 rather than amd64. So each
# is matched by shape, and the requirement is that the main package is there.
#
# Their subpackages are accepted but not required, because which of debuginfo,
# debugsource or -debug a distribution emits is that distribution's decision
# and not something this repository should assert.
DISTRIBUTION_PACKAGE_PATTERNS = {
    "fedora": r"kwin-effect-upscale-{version}-\d+\.fc\d+\.(?P<arch>x86_64|aarch64)\.rpm",
    "opensuse": r"kwin-effect-upscale-{version}-\d+\.(?P<arch>x86_64|aarch64)\.rpm",
    # Arch publishes no aarch64: the distribution supports one architecture and
    # the ARM port is a separate one with its own repositories.
    "arch": r"kwin-effect-upscale-{version}-\d+-(?P<arch>x86_64)\.pkg\.tar\.zst",
    # FreeBSD names a package after its manifest alone, because a pkg
    # repository is per architecture. These assets are not a repository, so the
    # build stamps the architecture into the file name like every other target.
    "freebsd": r"kwin-effect-upscale-{version}-(?P<arch>amd64)\.pkg",
}
# One per distribution whatever it was built for: a source package describes
# the tree, not the machine. FreeBSD has no entry: pkg has no source package,
# because on FreeBSD the ports tree is where a source recipe lives, and this
# repository is not it.
DISTRIBUTION_SOURCE_PATTERNS = {
    "fedora": r"kwin-effect-upscale-{version}-\d+\.fc\d+\.src\.rpm",
    "opensuse": r"kwin-effect-upscale-{version}-\d+\.src\.rpm",
    "arch": r"kwin-effect-upscale-{version}-\d+\.src\.tar\.gz",
}
DISTRIBUTION_SUBPACKAGE = r"kwin-effect-upscale-(?:debuginfo|debugsource|debug)-"

# What each distribution's job actually builds, and therefore what a complete
# release has to carry. Taken from the target table rather than inferred from
# the patterns above, which describe a name and not a matrix: Fedora and
# openSUSE publish both architectures, and Arch publishes x86_64 alone.
# Matching a subset would let a release ship whichever architectures succeeded.
DISTRIBUTION_ARCHITECTURES = {
    name: ci_targets.architectures(name) for name in DISTRIBUTION_PACKAGE_PATTERNS
}


def validate_version(version: str) -> str:
    """Reject malformed versions before using them as file or package names."""
    if re.fullmatch(VERSION_PATTERN, version) is None:
        message = f"Invalid release version: {version!r}"
        raise ValueError(message)
    return version


def package_changelog(version: str, commit: str, epoch: int, maintainer: str) -> str:
    """Derive the entire generated changelog entry from fixed build inputs."""
    date = email.utils.format_datetime(datetime.datetime.fromtimestamp(epoch, datetime.UTC))
    return (
        f"kwin-effect-upscale ({version}) unstable; urgency=medium\n\n"
        f"  * Build of commit {commit}.\n\n"
        f" -- {maintainer}  {date}\n\n"
    )


def digest(path: Path) -> str:
    """Hash a complete deliverable without retaining it in memory."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def package_field(path: Path, field: str) -> str:
    """Read package metadata rather than trusting its filename alone."""
    return subprocess.check_output(["dpkg-deb", "-f", str(path), field], text=True).strip()


def distribution_binaries(
    entries: set[str], expression: str, source_expression: str
) -> dict[str, str]:
    """Map each main package to the architecture read from its own name."""
    # Keep each match, so the architecture is read from the name that matched
    # rather than matched a second time to satisfy a type checker.
    main: dict[str, str] = {}
    for name in entries:
        found = re.fullmatch(expression, name)
        if found is None or re.fullmatch(source_expression, name):
            continue
        main[name] = found["arch"]
    return main


def distribution_subpackages(entries: set[str], expression: str) -> set[str]:
    """Return the debug companions that belong to one distribution's packages."""
    # A subpackage is the main name with debuginfo, debugsource or debug
    # inserted; strip that and it has to match the same shape.
    subpackages = set()
    for name in entries:
        if not re.match(DISTRIBUTION_SUBPACKAGE, name):
            continue
        stripped = re.sub(DISTRIBUTION_SUBPACKAGE, "kwin-effect-upscale-", name, count=1)
        if re.fullmatch(expression, stripped):
            subpackages.add(name)
    return subpackages


def validate_architectures(distribution: str, main: dict[str, str], version: str) -> None:
    """Require exactly one binary for every architecture the distribution builds.

    Two for the same architecture means two builds landed in the candidate, and
    which of them a user installs would then be decided by nothing. A missing
    one means a job failed, which is what this is for: the release would
    otherwise ship for whichever architectures happened to succeed.
    """
    if not main:
        message = f"No {distribution} package for {version} in the release candidate"
        raise ValueError(message)
    architectures = list(main.values())
    if len(set(architectures)) != len(architectures):
        message = f"More than one {distribution} package per architecture: {sorted(main)}"
        raise ValueError(message)
    missing = DISTRIBUTION_ARCHITECTURES[distribution] - set(architectures)
    if missing:
        message = (
            f"No {distribution} {', '.join(sorted(missing))} package for {version} "
            f"in the release candidate"
        )
        raise ValueError(message)


def validate_distribution_assets(entries: set[str], version: str) -> set[str]:
    """Return the Fedora, openSUSE and Arch assets, requiring every build.

    A release that silently lost a distribution or one of its architectures is
    the failure this prevents: the nightly builds all three distributions, and
    two of them for both architectures, so anything missing from a candidate
    means a job failed and its absence would otherwise go unnoticed.
    """
    escaped = re.escape(version)
    recognized: set[str] = set()
    for distribution, pattern in DISTRIBUTION_PACKAGE_PATTERNS.items():
        expression = pattern.format(version=escaped)
        source_pattern = DISTRIBUTION_SOURCE_PATTERNS.get(distribution)
        # A pattern nothing can match, for a distribution that ships no source
        # package: the binaries are then selected without excluding one.
        source_expression = source_pattern.format(version=escaped) if source_pattern else r"(?!)"
        main = distribution_binaries(entries, expression, source_expression)
        validate_architectures(distribution, main, version)
        sources = {name for name in entries if re.fullmatch(source_expression, name)}
        # A distribution's contract is the binary, its debug symbols and the
        # source it was built from. The binaries are per architecture; the
        # source is one file, so exactly one of it is required where the
        # distribution has the concept at all.
        if source_pattern and len(sources) != 1:
            message = (
                f"Expected exactly one {distribution} source package for {version}, "
                f"found {sorted(sources)}"
            )
            raise ValueError(message)
        recognized |= set(main) | distribution_subpackages(entries, expression) | sources
    return recognized


def validate_assets(directory: Path, version: str) -> list[Path]:
    """Require exactly the supported package matrix and its build records."""
    validate_version(version)
    expected = {f"kwin-effect-upscale-{version}.tar.gz"}
    packages: list[Path] = []
    for distribution in DISTRIBUTIONS:
        for architecture in ARCHITECTURES:
            suffix = f"{version}~{distribution}_{architecture}"
            main = directory / f"kwin-effect-upscale_{suffix}.deb"
            debug = directory / (
                f"kwin-effect-upscale-dbgsym_{suffix}."
                + ("deb" if distribution == "trixie" else "ddeb")
            )
            expected.update((main.name, debug.name))
            expected.update(
                f"kwin-effect-upscale_{suffix}.{ext}" for ext in ("buildinfo", "changes")
            )
            for package, name in (
                (main, "kwin-effect-upscale"),
                (debug, "kwin-effect-upscale-dbgsym"),
            ):
                if not package.is_file() or package.is_symlink():
                    message = f"Missing regular package: {package.name}"
                    raise ValueError(message)
                for field, value in (
                    ("Package", name),
                    ("Version", f"{version}~{distribution}"),
                    ("Architecture", architecture),
                ):
                    if package_field(package, field) != value:
                        message = f"Unexpected {field} in {package.name}"
                        raise ValueError(message)
                packages.append(package)
        # One source package per distribution, not per architecture: it
        # describes the tree, so dpkg-buildpackage builds it once, on amd64.
        expected.update(
            f"kwin-effect-upscale_{version}~{distribution}.{ext}" for ext in ("dsc", "tar.xz")
        )
    entries = {path.name for path in directory.iterdir()}
    extra = validate_distribution_assets(entries, version)
    if entries - extra != expected:
        missing = expected - entries
        unexpected = entries - extra - expected
        message = f"Missing assets: {missing}; unexpected assets: {unexpected}"
        raise ValueError(message)
    paths = sorted(directory.iterdir())
    if any(not path.is_file() or path.is_symlink() or path.stat().st_size == 0 for path in paths):
        message = "Release assets must be nonempty regular files"
        raise ValueError(message)
    return paths


def installable_package(
    names: dict[str, Path], identifier: str, architecture: str, version: str
) -> Path:
    """Find the one package a person installs, out of everything published."""
    entry = ci_targets.target(identifier)
    if entry.family == "deb":
        # After the public rename: dpkg's '~' separator became a '.'.
        wanted = f"kwin-effect-upscale_{version}.{identifier}_{architecture}.deb"
        found = [names[wanted]] if wanted in names else []
    else:
        expression = DISTRIBUTION_PACKAGE_PATTERNS[identifier].format(version=re.escape(version))
        native = ci_targets.native_architecture(identifier, architecture)
        found = [
            path
            for name, path in names.items()
            if (match := re.fullmatch(expression, name)) and match["arch"] == native
        ]
    if len(found) != 1:
        message = f"Expected one installable {identifier} {architecture} package, found {found}"
        raise ValueError(message)
    return found[0]


def write_download_aliases(paths: list[Path], version: str) -> list[Path]:
    """Publish each installable package a second time under a stable name.

    A release carries forty files, and the one a person needs is not the one
    with the shortest name. These copies are what the README links to: their
    names survive a new version and a new distribution release, so the link
    never has to be rewritten. They are made before the manifest, so the
    checksums and the attestation cover them like everything else.
    """
    names = {path.name: path for path in paths}
    created = []
    for entry in ci_targets.TARGETS:
        for architecture in entry.architectures:
            source = installable_package(names, entry.identifier, architecture, version)
            alias = source.with_name(ci_targets.download_name(entry.identifier, architecture))
            shutil.copy2(source, alias)
            created.append(alias)
    return created


def write_manifest(directory: Path, version: str) -> None:
    """Only produce a manifest after the full release inventory is validated."""
    paths = validate_assets(directory, version)
    # GitHub replaces '~' in asset filenames with '.'. Choose those public
    # names before checksumming and attesting, so downloaded manifests work.
    # Package versions and Debian's original build records remain unchanged.
    paths = [path.rename(path.with_name(path.name.replace("~", "."))) for path in paths]
    paths = sorted(paths + write_download_aliases(paths, version))
    (directory / "SHA256SUMS").write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in paths)
    )
