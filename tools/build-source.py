#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create the published source archive and build its extracted contents."""

import argparse
import os
import subprocess
import tarfile
from pathlib import Path

from release_assets import validate_version


def git(*arguments: str) -> str:
    """Ask Git, or answer nothing where it cannot say."""
    result = subprocess.run(["git", *arguments], capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else ""


def provenance(revision: str) -> dict[str, str]:
    """Where the archived sources came from, as far as it is known.

    The build of the extracted archive has no Git, and reads these beside the
    version. A tree archived for a local check is no commit and has no branch,
    so for it nothing is recorded rather than something guessed.
    """
    commit = git("rev-parse", "--verify", "--quiet", f"{revision}^{{commit}}")
    if not commit:
        return {}
    recorded = {"commit": commit}
    if os.environ.get("GITHUB_REF_TYPE") == "tag" and os.environ.get("GITHUB_REF_NAME"):
        recorded["tag"] = os.environ["GITHUB_REF_NAME"]
    else:
        branch = os.environ.get("GITHUB_HEAD_REF") or os.environ.get("GITHUB_REF_NAME")
        if not branch and git("rev-parse", "HEAD") == commit:
            branch = git("symbolic-ref", "--short", "--quiet", "HEAD")
        if branch:
            recorded["branch"] = branch
        tag = git("describe", "--tags", "--exact-match", commit)
        if tag:
            recorded["tag"] = tag
    return recorded


def main() -> None:
    """Test the archive without access to the checkout's Git metadata."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", type=validate_version)
    parser.add_argument(
        "--revision",
        default="HEAD",
        help="Git object to archive; a tree ID permits local checks before committing",
    )
    arguments = parser.parse_args()
    root = Path.cwd()
    artifacts = root / "build/artifacts"
    artifacts.mkdir(parents=True, exist_ok=True)
    name = f"kwin-effect-upscale-{arguments.version}"
    metadata = root / "build/source-version"
    metadata.write_text(
        "# SPDX-FileCopyrightText: None\n# SPDX-License-Identifier: CC0-1.0\n"
        + arguments.version
        + "\n"
        + "".join(f"{field}={value}\n" for field, value in provenance(arguments.revision).items())
    )
    archive = artifacts / f"{name}.tar.gz"
    subprocess.run(
        [
            "git",
            "archive",
            "--format=tar.gz",
            f"--prefix={name}/",
            f"--add-file={metadata}",
            "-o",
            str(archive),
            arguments.revision,
        ],
        check=True,
    )
    extracted = root / "build/extracted"
    extracted.mkdir(exist_ok=True)
    source = extracted / name
    if source.exists():
        message = "Extracted-source validation requires a fresh directory"
        raise ValueError(message)
    with tarfile.open(archive) as stream:
        stream.extractall(extracted, filter="data")
    build = source / "build"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(source),
            "-B",
            str(build),
            "-G",
            "Ninja",
            "-DBUILD_TESTING=ON",
            "-DCMAKE_COMPILE_WARNING_AS_ERROR=ON",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build)], check=True)
    subprocess.run(
        ["python3", "-B", str(source / "tools/run-render-tests.py")],
        check=True,
        env={**os.environ, "UPSCALE_BUILD_DIR": str(build)},
    )
    stage = root / "build/source-stage"
    subprocess.run(
        ["cmake", "--install", str(build)],
        check=True,
        env={**os.environ, "DESTDIR": str(stage)},
    )
    # What the archive installs has to carry the notices its sources carry.
    if not list(stage.rglob("kwin-effect-upscale/third-party-notices.md")):
        message = "the extracted source archive installs no third-party notices"
        raise RuntimeError(message)


if __name__ == "__main__":
    main()
