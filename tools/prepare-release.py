#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the full release inventory before checksum generation and signing."""

import argparse
from pathlib import Path

from release_assets import validate_version, write_manifest


def main() -> None:
    """Require explicit version and artifact directory arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", type=validate_version)
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()
    write_manifest(arguments.directory, arguments.version)


if __name__ == "__main__":
    main()
