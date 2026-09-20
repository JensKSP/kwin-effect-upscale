#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build one Debian-family package set inside containers/package.

    build-deb-package.py <distribution> <version>

Debian is built twice from separate clean sources and compared byte for
byte; every other target is built once. The test suite does not run here:
it runs once afterwards, against the installed package.
"""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

import ci_targets
from release_assets import DISTRIBUTIONS, digest, package_changelog, validate_version

EXPECTED_PACKAGE_COUNT = 2


def output(*command: str, cwd: Path) -> str:
    """Read a build identity or metadata field."""
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def build(root: Path, destination: Path, version: str, epoch: int) -> list[Path]:
    """Build a separate source copy, keeping all generated files under build/."""
    source = destination / "source"
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    subprocess.run(
        ["git", "clone", "--quiet", "--no-hardlinks", str(root), str(source)], check=True
    )
    # Include the reviewed working tree during local validation. Hosted release
    # jobs check out the exact tag/commit and therefore overlay identical files.
    for name in output("git", "ls-files", cwd=root).splitlines():
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / name, target)
    maintainer = output("dpkg-parsechangelog", "-S", "Maintainer", cwd=source)
    commit = output("git", "rev-parse", "HEAD", cwd=root)
    changelog = source / "debian/changelog"
    changelog.write_text(
        package_changelog(version, commit, epoch, maintainer) + changelog.read_text()
    )
    # dpkg-buildpackage selects native parallelism and respects DEB_BUILD_OPTIONS.
    # nocheck because the suite runs on the installed package in the test stage,
    # where it has a job to itself: running it here would run it once per clean
    # build, against a tree rather than against what the package ships.
    options = " ".join(filter(None, (os.environ.get("DEB_BUILD_OPTIONS", ""), "nocheck")))
    environment = {**os.environ, "SOURCE_DATE_EPOCH": str(epoch), "DEB_BUILD_OPTIONS": options}
    # -F builds the source package as well, -b the binaries alone. The source
    # package describes the tree and not the machine, so building it on both
    # architectures would produce the same two files twice under one name. It
    # is built where the rest of the source deliverables are, on amd64.
    mode = "-F" if output("dpkg", "--print-architecture", cwd=source) == "amd64" else "-b"
    with (destination / "build.log").open("w") as log:
        result = subprocess.run(
            ["dpkg-buildpackage", mode, "-us", "-uc"],
            cwd=source,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode:
        print((destination / "build.log").read_text())
        raise subprocess.CalledProcessError(result.returncode, "dpkg-buildpackage")
    packages = sorted([*destination.glob("*.deb"), *destination.glob("*.ddeb")])
    if len(packages) != EXPECTED_PACKAGE_COUNT:
        message = f"Expected main and debug packages, found {packages}"
        raise ValueError(message)
    subprocess.run(
        [
            "pre-commit",
            "run",
            "debian-package-lint",
            "--hook-stage",
            "manual",
            "--files",
            *(str(p) for p in packages),
        ],
        check=True,
    )
    return packages


def publish(root: Path) -> None:
    """Stage the first build's deliverables, whether or not a second ran."""
    artifacts = root / "build/artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    for pattern in ("*.deb", "*.ddeb", "*.buildinfo", "*.changes", "*.dsc", "*.tar.xz"):
        for path in (root / "build/packages/first").glob(pattern):
            shutil.copy2(path, artifacts / path.name)


def main() -> None:
    """Build once, and for a reproducible target compare a second clean build."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=DISTRIBUTIONS)
    parser.add_argument("version", type=validate_version)
    arguments = parser.parse_args()
    root = Path.cwd()
    os.environ["PRE_COMMIT_HOME"] = str(root / "build/pre-commit")
    epoch = int(output("git", "show", "-s", "--format=%ct", "HEAD", cwd=root))
    version = f"{arguments.version}~{arguments.distribution}"
    first = build(root, root / "build/packages/first", version, epoch)
    # One target carries the reproducibility comparison for all of them: it
    # finds nondeterminism in the build, which is a property of the sources and
    # not of the distribution, and a second build of every target would pay for
    # the same answer five more times.
    if not ci_targets.target(arguments.distribution).reproducible:
        publish(root)
        print(f"{arguments.distribution}: one clean build, as this target is not compared.")
        return
    second = build(root, root / "build/packages/second", version, epoch)
    if {p.name: digest(p) for p in first} != {p.name: digest(p) for p in second}:
        message = "The two clean package builds differ; retain both builds for diagnosis"
        raise ValueError(message)
    publish(root)
    print("Both clean builds produced identical packages.")


if __name__ == "__main__":
    main()
