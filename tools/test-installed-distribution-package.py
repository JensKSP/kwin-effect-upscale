#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install a distribution package in a clean container, load it, remove it.

    test-installed-distribution-package.py <distribution> <package>

Run only in a disposable container of that distribution, as root. The Debian
path has tools/test-installed-package.py; this is the same lifecycle for the
package managers that do not speak dpkg.
"""

import argparse
import ctypes
import subprocess
import sys
from pathlib import Path

NAME = "kwin-effect-upscale"

# Installing a local file, listing what a package owns, and removing it. The
# three managers disagree about all of it, and about nothing else here.
INSTALL = {
    "fedora": ("dnf", "install", "-y", "--setopt=install_weak_deps=False"),
    "opensuse": ("zypper", "--non-interactive", "install", "--allow-unsigned-rpm"),
    "arch": ("pacman", "-U", "--noconfirm"),
}
CONTENTS = {
    "fedora": ("rpm", "-ql", NAME),
    "opensuse": ("rpm", "-ql", NAME),
    "arch": ("pacman", "-Ql", NAME),
}
REMOVE = {
    "fedora": ("dnf", "remove", "-y", NAME),
    "opensuse": ("zypper", "--non-interactive", "remove", NAME),
    "arch": ("pacman", "-R", "--noconfirm", NAME),
}


def run(*command: str) -> None:
    """Fail the check on any unsuccessful lifecycle operation."""
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def installed_files(distribution: str) -> list[str]:
    """List the installed paths, whichever way this manager reports them."""
    output = subprocess.check_output(CONTENTS[distribution], text=True)
    if distribution == "arch":
        # pacman -Ql prints "<package> <path>", and directories as well.
        return [line.split(" ", 1)[1] for line in output.splitlines() if " " in line]
    return output.splitlines()


def load(library: str) -> None:
    """Load the plugin the way a compositor would, and find its entry point.

    An installed file is not a loadable plugin. This resolves every shared
    library the plugin was linked against and then the symbol Qt calls to
    instantiate it, which is what a missing runtime dependency breaks and what
    a file listing cannot tell us.
    """
    try:
        handle = ctypes.CDLL(library)
    except OSError as failure:
        message = f"{library} does not load: {failure}"
        raise ValueError(message) from failure
    if not hasattr(handle, "qt_plugin_instance"):
        message = f"{library} loaded but exports no Qt plugin entry point"
        raise ValueError(message)
    print(f"  loaded {library}", flush=True)


def main() -> int:
    """Run only in a disposable clean distribution container, as root."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("distribution", choices=sorted(INSTALL))
    parser.add_argument("package", type=Path)
    arguments = parser.parse_args()
    package = str(arguments.package.resolve())
    distribution = arguments.distribution

    run(*INSTALL[distribution], package)
    files = installed_files(distribution)
    libraries = [name for name in files if name.endswith(".so")]
    effect = next(name for name in libraries if name.endswith("/upscale.so"))
    config = next(name for name in libraries if name.endswith("/kwin_upscale_config.so"))
    if "/kwin/effects/plugins/" not in effect:
        message = f"Effect installed outside KWin's plugin search path: {effect}"
        raise ValueError(message)
    if not any(name.endswith("/xdg/kwinupscalerc") for name in files):
        message = "The package does not install the effect's own defaults"
        raise ValueError(message)
    load(effect)
    load(config)

    # Reinstalling over an installed copy is the upgrade path's first half;
    # upgrading from an older published version stays a separate acceptance case.
    run(*INSTALL[distribution], package)
    load(effect)
    run(*REMOVE[distribution])
    remaining = [name for name in libraries if Path(name).exists()]
    if remaining:
        message = f"Removing the package left plugin libraries installed: {remaining}"
        raise ValueError(message)
    print(f"{distribution}: install, load, reinstall and removal passed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
