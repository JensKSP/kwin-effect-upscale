#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build one target's package, whatever packaging its distribution uses.

    build-package.py <target> <version>

Run it in that target's environment: the container under `containers/` for a
container target, the virtual machine for FreeBSD. The package lands in
`build/artifacts/`.

This is the only packaging entry point a workflow calls, so that adding a
distribution never adds a branch to a workflow. Which builder runs is decided
here, from the family in the target table: `debian/` and dpkg-buildpackage for
the Debian family, a filled-in recipe under `packaging/` for everyone else.
"""

import argparse
import subprocess
import sys
from pathlib import Path

from ci_targets import BY_IDENTIFIER, target

BUILDERS = {
    "deb": "build-deb-package.py",
    "rpm": "build-recipe-package.py",
    "arch": "build-recipe-package.py",
    "pkg": "build-recipe-package.py",
}


def main() -> None:
    """Hand the target and version to the builder its family names."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=sorted(BY_IDENTIFIER))
    parser.add_argument("version")
    arguments = parser.parse_args()
    builder = Path(__file__).resolve().parent / BUILDERS[target(arguments.target).family]
    subprocess.run(
        [sys.executable, "-B", str(builder), arguments.target, arguments.version], check=True
    )


if __name__ == "__main__":
    main()
