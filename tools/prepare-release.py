#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the full release inventory before checksum generation and signing."""

import argparse
import datetime
import os
import subprocess
from pathlib import Path

from release_assets import validate_version, write_manifest
from release_sbom import Origin

ROOT = Path(__file__).resolve().parent.parent


def origin(version: str, channel: str) -> Origin:
    """Collect what the release files do not say about where they come from."""
    commit = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True)
    # The commit's own time unless the build fixed one, so that preparing the
    # same commit again describes it in the same words.
    epoch = os.environ.get("SOURCE_DATE_EPOCH") or subprocess.check_output(
        ["git", "-C", str(ROOT), "show", "--no-patch", "--format=%ct", "HEAD"], text=True
    )
    return Origin(
        repository=os.environ["GITHUB_REPOSITORY"],
        tag="nightly" if channel == "nightly" else f"v{version}",
        commit=commit.strip(),
        created=datetime.datetime.fromtimestamp(int(epoch), datetime.UTC),
        source=ROOT,
    )


def main() -> None:
    """Require explicit version, channel and artifact directory arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", type=validate_version)
    parser.add_argument("channel", choices=("release", "nightly"))
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()
    write_manifest(
        arguments.directory, arguments.version, origin(arguments.version, arguments.channel)
    )


if __name__ == "__main__":
    main()
