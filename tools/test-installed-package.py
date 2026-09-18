#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check installation, reinstall, plugin factory loading, removal and purge."""

import argparse
import subprocess
from pathlib import Path


def run(*command: str) -> None:
    """Fail the package check on any unsuccessful lifecycle operation."""
    subprocess.run(command, check=True)


def main() -> None:
    """Run only in a disposable clean distribution container as root."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("probe", type=Path)
    arguments = parser.parse_args()
    package = str(arguments.package.resolve())
    run("apt-get", "update")
    run("apt-get", "install", "--no-install-recommends", "-y", package)
    listing = subprocess.check_output(["dpkg-query", "-L", "kwin-effect-upscale"], text=True)
    libraries = [name for name in listing.splitlines() if name.endswith(".so")]
    effect = next(name for name in libraries if name.endswith("/upscale.so"))
    config = next(name for name in libraries if name.endswith("/kwin_upscale_config.so"))
    if "/kwin/effects/plugins/" not in effect:
        message = f"Effect installed outside KWin's plugin search path: {effect}"
        raise ValueError(message)
    run(str(arguments.probe.resolve()), effect, config)
    # Reinstall exercises unpack/configure over an installed copy; upgrades from
    # an older published version remain a separate acceptance case.
    run("apt-get", "install", "--reinstall", "--no-install-recommends", "-y", package)
    run(str(arguments.probe.resolve()), effect, config)
    run("apt-get", "remove", "-y", "kwin-effect-upscale")
    if any(Path(path).exists() for path in libraries):
        message = "Removing the package left plugin libraries installed"
        raise ValueError(message)
    # This package has no conffiles: removal forgets it completely, so apt can
    # no longer locate it in repository indices. Purge a newly installed copy.
    run("apt-get", "install", "--no-install-recommends", "-y", package)
    run("apt-get", "purge", "-y", "kwin-effect-upscale")
    if any(Path(path).exists() for path in libraries):
        message = "Purging the package left plugin libraries installed"
        raise ValueError(message)


if __name__ == "__main__":
    main()
