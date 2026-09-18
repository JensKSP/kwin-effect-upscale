#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Resolve workflow versions from the project's declaration and checked-out commit."""

import argparse
import os
import re
import subprocess
from pathlib import Path

from release_assets import validate_version


def main() -> None:
    """Validate a release tag or print the deterministic snapshot version."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("channel", choices=("release", "snapshot"))
    arguments = parser.parse_args()
    match = re.search(
        r"project\(kwin-effect-upscale VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
        Path("CMakeLists.txt").read_text(),
    )
    if not match:
        parser.error("Cannot find project VERSION")
    version = match[1]
    if arguments.channel == "release":
        tag = os.environ["GITHUB_REF_NAME"]
        changelog = subprocess.check_output(
            ["dpkg-parsechangelog", "-S", "Version"], text=True
        ).strip()
        if tag != f"v{version}" or changelog != version:
            parser.error(f"Version mismatch: tag={tag}, project={version}, changelog={changelog}")
    else:
        date = subprocess.check_output(["git", "show", "-s", "--format=%cs", "HEAD"], text=True)
        commit = subprocess.check_output(["git", "rev-parse", "--short=10", "HEAD"], text=True)
        version += f"+git{date.strip().replace('-', '')}.{commit.strip()}"
    print(f"version={validate_version(version)}")


if __name__ == "__main__":
    main()
