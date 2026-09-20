#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Install one built package in a clean environment, load it, remove it.

    test-package.py <target> <package-or-directory> [--probe <binary>]

Run only in a disposable clean environment of that target, as root: a container
of the distribution, or the FreeBSD virtual machine. Nothing but the package
and a Python interpreter is installed, which is what makes a missing runtime
dependency fail here and nowhere else. The package managers disagree about
installing a local file, listing what a package owns and removing it, and about
nothing else this check does.
"""

import argparse
import ctypes
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import ci_targets

NAME = "kwin-effect-upscale"


@dataclass(frozen=True)
class Manager:
    """One package manager's spelling of the lifecycle this check exercises."""

    install: tuple[str, ...]
    contents: tuple[str, ...]
    remove: tuple[str, ...]
    refresh: tuple[str, ...] = ()
    # Purging is dpkg's alone. Elsewhere removal already forgets the package,
    # so there is no second state to check.
    purge: tuple[str, ...] = ()
    # pacman prints "<package> <path>" and lists directories as well.
    prefixed_contents: bool = field(default=False)


APT = Manager(
    install=("apt-get", "install", "--no-install-recommends", "-y"),
    contents=("dpkg-query", "-L", NAME),
    remove=("apt-get", "remove", "-y", NAME),
    refresh=("apt-get", "update"),
    purge=("apt-get", "purge", "-y", NAME),
)
MANAGERS = {
    "trixie": APT,
    "resolute": APT,
    "fedora": Manager(
        install=("dnf", "install", "-y", "--setopt=install_weak_deps=False"),
        contents=("rpm", "-ql", NAME),
        remove=("dnf", "remove", "-y", NAME),
    ),
    "opensuse": Manager(
        install=("zypper", "--non-interactive", "install", "--allow-unsigned-rpm"),
        contents=("rpm", "-ql", NAME),
        remove=("zypper", "--non-interactive", "remove", NAME),
    ),
    "arch": Manager(
        install=("pacman", "-U", "--noconfirm"),
        contents=("pacman", "-Ql", NAME),
        remove=("pacman", "-R", "--noconfirm", NAME),
        prefixed_contents=True,
    ),
    # -f because the package is a local file built minutes ago, which pkg has
    # no repository entry for and would otherwise decline to reinstall over.
    "freebsd": Manager(
        install=("pkg", "add", "-f"),
        contents=("pkg", "query", "%Fp", NAME),
        remove=("pkg", "delete", "-y", NAME),
    ),
}


# The one package that is not a debug sidecar or a source package, chosen by
# glob rather than by parsing a directory listing, which cannot survive an
# unusual file name. Each family stamps its own release, distribution or
# architecture into the name, so the shape is all there is to match on.
MAIN_PACKAGE = {
    "deb": ("*.deb", ("-dbgsym_",)),
    "rpm": ("*.rpm", ("-debuginfo-", "-debugsource-", ".src.rpm")),
    "arch": ("*.pkg.tar.zst", ("-debug-",)),
    "pkg": ("*.pkg", ()),
}


def select_package(directory: Path, family: str) -> Path:
    """Pick the installable package out of everything the build stage uploaded."""
    pattern, rejected = MAIN_PACKAGE[family]
    found = [
        path
        for path in sorted(directory.glob(pattern))
        if not any(mark in path.name for mark in rejected)
    ]
    if len(found) != 1:
        message = f"Expected one {family} package in {directory}, found {[p.name for p in found]}"
        raise ValueError(message)
    return found[0]


def run(*command: str) -> None:
    """Fail the check on any unsuccessful lifecycle operation."""
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def installed_files(manager: Manager) -> list[str]:
    """List the installed paths, whichever way this manager reports them."""
    output = subprocess.check_output(manager.contents, text=True)
    if manager.prefixed_contents:
        return [line.split(" ", 1)[1] for line in output.splitlines() if " " in line]
    return output.splitlines()


def load(library: str) -> None:
    """Load the plugin the way a compositor would, and find its entry point.

    An installed file is not a loadable plugin. This resolves every shared
    library the plugin was linked against and then the symbol Qt calls to
    instantiate it, which is what a missing runtime dependency breaks and what
    a file listing cannot tell us. Where the build produced the probe, that one
    runs instead and goes further: it constructs the factory through Qt.
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


def plugins(manager: Manager) -> tuple[str, str]:
    """Find the two installed plugins and refuse a package that misplaces them."""
    files = installed_files(manager)
    libraries = [name for name in files if name.endswith(".so")]
    effect = next(name for name in libraries if name.endswith("/upscale.so"))
    config = next(name for name in libraries if name.endswith("/kwin_upscale_config.so"))
    if "/kwin/effects/plugins/" not in effect:
        message = f"Effect installed outside KWin's plugin search path: {effect}"
        raise ValueError(message)
    if not any(name.endswith("/xdg/kwinupscalerc") for name in files):
        message = "The package does not install the effect's own defaults"
        raise ValueError(message)
    return effect, config


def verify(effect: str, config: str, probe: Path | None) -> None:
    """Load both plugins, through the probe when the build produced one."""
    if probe is None:
        load(effect)
        load(config)
        return
    run(str(probe.resolve()), effect, config)


def main() -> int:
    """Run only in a disposable clean environment of the target, as root."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=sorted(MANAGERS))
    parser.add_argument("package", type=Path)
    parser.add_argument("--probe", type=Path, default=None)
    parser.add_argument(
        "--select",
        action="store_true",
        help="print the installable package this would test, and do nothing else",
    )
    arguments = parser.parse_args()
    manager = MANAGERS[arguments.target]
    entry = ci_targets.target(arguments.target)
    chosen = arguments.package
    if chosen.is_dir():
        chosen = select_package(chosen, entry.family)
    if arguments.select:
        print(chosen)
        return 0
    print(f"Testing {chosen.name}", flush=True)
    package = str(chosen.resolve())
    label = entry.label

    if manager.refresh:
        run(*manager.refresh)
    run(*manager.install, package)
    effect, config = plugins(manager)
    libraries = [effect, config]
    verify(effect, config, arguments.probe)

    # Reinstalling over an installed copy is the upgrade path's first half;
    # upgrading from an older published version stays a separate acceptance case.
    run(*manager.install, package)
    verify(effect, config, arguments.probe)

    run(*manager.remove)
    remaining = [name for name in libraries if Path(name).exists()]
    if remaining:
        message = f"Removing the package left plugin libraries installed: {remaining}"
        raise ValueError(message)

    if manager.purge:
        # This package has no conffiles: removal forgets it completely, so the
        # manager can no longer locate it. Purge a newly installed copy.
        run(*manager.install, package)
        run(*manager.purge)
        remaining = [name for name in libraries if Path(name).exists()]
        if remaining:
            message = f"Purging the package left plugin libraries installed: {remaining}"
            raise ValueError(message)

    print(f"{label}: install, load, reinstall and removal passed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
