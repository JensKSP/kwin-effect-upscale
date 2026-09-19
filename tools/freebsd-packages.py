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
    "libxcb-shm0-dev": ("libxcb",),
    "libxcb-sync-dev": ("libxcb",),
    "xwayland": ("xwayland",),
    "libxkbcommon-dev": ("libxkbcommon",),
    "ninja-build": ("ninja",),
    "pkgconf": ("pkgconf",),
    "qt6-base-dev": ("qt6-base",),
    "wayland-protocols": ("wayland-protocols",),
}


def main() -> None:
    """Print the translated dependencies for pkg install, rejecting unknown names."""
    match = re.search(
        r"Build-Depends:([^\n]*(?:\n[ \t]+[^\n]*)*)", Path("debian/control").read_text()
    )
    if match is None:
        message = "Missing Build-Depends in debian/control"
        raise ValueError(message)
    dependencies = {entry.split()[0] for entry in match[1].split(",") if entry.strip()}
    print(" ".join(sorted({package for name in dependencies for package in PACKAGES[name]})))


if __name__ == "__main__":
    main()
