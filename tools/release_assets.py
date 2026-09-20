# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate release contents and create deterministic package metadata."""

import datetime
import email.utils
import hashlib
import re
import subprocess
from pathlib import Path

VERSION_PATTERN = r"[0-9]+\.[0-9]+\.[0-9]+(?:\+git[0-9]{8}\.[0-9a-f]{10})?"
DISTRIBUTIONS = ("trixie", "resolute")
ARCHITECTURES = ("amd64", "arm64")

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
    "fedora": r"kwin-effect-upscale-{version}-\d+\.fc\d+\.(?:x86_64|aarch64)\.rpm",
    "opensuse": r"kwin-effect-upscale-{version}-\d+\.(?:x86_64|aarch64)\.rpm",
    "arch": r"kwin-effect-upscale-{version}-\d+-(?:x86_64|aarch64)\.pkg\.tar\.zst",
}
DISTRIBUTION_SUBPACKAGE = r"kwin-effect-upscale-(?:debuginfo|debugsource|debug)-"


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


def validate_distribution_assets(entries: set[str], version: str) -> set[str]:
    """Return the Fedora, openSUSE and Arch assets, requiring one of each.

    A release that silently lost a distribution is the failure this prevents:
    the nightly builds all three, so a candidate carrying only two of them
    means a job failed and its absence would otherwise go unnoticed.
    """
    escaped = re.escape(version)
    recognized: set[str] = set()
    for distribution, pattern in DISTRIBUTION_PACKAGE_PATTERNS.items():
        expression = pattern.format(version=escaped)
        main = {name for name in entries if re.fullmatch(expression, name)}
        # A subpackage is the main name with debuginfo, debugsource or
        # debug inserted; strip that and it has to match the same shape.
        subpackages = set()
        for name in entries:
            if not re.match(DISTRIBUTION_SUBPACKAGE, name):
                continue
            stripped = re.sub(DISTRIBUTION_SUBPACKAGE, "kwin-effect-upscale-", name, count=1)
            if re.fullmatch(expression, stripped):
                subpackages.add(name)
        if not main:
            message = f"No {distribution} package for {version} in the release candidate"
            raise ValueError(message)
        recognized |= main | subpackages
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


def write_manifest(directory: Path, version: str) -> None:
    """Only produce a manifest after the full release inventory is validated."""
    paths = validate_assets(directory, version)
    # GitHub replaces '~' in asset filenames with '.'. Choose those public
    # names before checksumming and attesting, so downloaded manifests work.
    # Package versions and Debian's original build records remain unchanged.
    paths = [path.rename(path.with_name(path.name.replace("~", "."))) for path in paths]
    (directory / "SHA256SUMS").write_text(
        "".join(f"{digest(path)}  {path.name}\n" for path in paths)
    )
