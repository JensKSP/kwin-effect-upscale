#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Translate debian/control build dependencies into FreeBSD package names."""

import re
from pathlib import Path

# FreeBSD packages include development headers. This is a name translation, not
# an independent dependency list: new Debian requirements must have a mapping.
PACKAGES = {
    "debhelper-compat": (),
    "dbus-daemon": ("dbus",),
    "cmake": ("cmake",),
    "extra-cmake-modules": ("kf6-extra-cmake-modules",),
    "gettext": ("gettext-tools",),
    "kwin-dev": ("plasma6-kwin",),
    "kwin-wayland": ("plasma6-kwin",),
    "libdrm-dev": ("libdrm",),
    "libepoxy-dev": ("libepoxy",),
    "libegl-mesa0": ("mesa-libs",),
    "libgl1-mesa-dri": ("mesa-dri",),
    "libgles2": ("mesa-libs",),
    "libkf6config-dev": ("kf6-kconfig",),
    "libkf6configwidgets-dev": ("kf6-kconfigwidgets",),
    "libkf6coreaddons-dev": ("kf6-kcoreaddons",),
    "libkf6i18n-dev": ("kf6-ki18n",),
    "libkf6kcmutils-dev": ("kf6-kcmutils",),
    "libwayland-dev": ("wayland",),
    "libwayland-bin": ("wayland",),
    "libxcb-composite0-dev": ("libxcb",),
    "libxcb-randr0-dev": ("libxcb",),
    "libxcb-res0-dev": ("libxcb",),
    "libxcb-shm0-dev": ("libxcb",),
    "libxcb-sync-dev": ("libxcb",),
    "xwayland": ("xwayland",),
    "libxkbcommon-dev": ("libxkbcommon",),
    "ninja-build": ("ninja",),
    "pkgconf": ("pkgconf",),
    "qt6-base-dev": ("qt6-base",),
    "wayland-protocols": ("wayland-protocols",),
}


def translate(control: str) -> list[str]:
    """Return the FreeBSD packages for a debian/control, rejecting unknown names."""
    match = re.search(r"Build-Depends:([^\n]*(?:\n[ \t]+[^\n]*)*)", control)
    if match is None:
        message = "Missing Build-Depends in debian/control"
        raise ValueError(message)
    dependencies = {entry.split()[0] for entry in match[1].split(",") if entry.strip()}
    # Naming the file to edit matters more here than elsewhere: this runs
    # inside the nightly's FreeBSD virtual machine, where nobody is watching.
    unknown = sorted(dependencies - PACKAGES.keys())
    if unknown:
        message = (
            f"debian/control lists {', '.join(unknown)} without a FreeBSD package name. "
            "Add the translation to tools/freebsd-packages.py."
        )
        raise ValueError(message)
    return sorted({package for name in dependencies for package in PACKAGES[name]})


def main() -> None:
    """Print the translated dependencies for pkg install."""
    print(" ".join(translate(Path("debian/control").read_text())))


if __name__ == "__main__":
    main()
