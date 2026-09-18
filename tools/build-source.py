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
    subprocess.run(
        ["cmake", "--install", str(build)],
        check=True,
        env={**os.environ, "DESTDIR": str(root / "build/source-stage")},
    )


if __name__ == "__main__":
    main()
