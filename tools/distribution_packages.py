# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Translate debian/control build dependencies into other distributions' names.

`debian/control` is the repository's only list of build dependencies. Every
other packaging target derives its own names from it here, so that adding a
dependency in one place cannot leave another distribution silently building
without it. A name with no mapping is an error, never an omission: the build
that discovers it usually runs unattended, so the message names the dependency
and this file.

These are name translations, not independent dependency lists. Where a
distribution splits or merges Debian's packaging, several Debian names map onto
one name here, or onto none at all when the distribution has no equivalent.
"""

import re

# Debian ships headers separately from libraries; the -dev package is the one
# that matters. Fedora and openSUSE do the same with -devel, while Arch ships
# headers inside the library package, so most Arch entries lose the suffix.
FREEBSD = {
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

# Fedora keeps KWin's effect headers in kwin-devel, and its kwin package is the
# Wayland compositor: the X11 one is split off as kwin-x11 and is not wanted
# here. EGL and GLES come from libglvnd rather than from Mesa directly.
FEDORA = {
    "debhelper-compat": (),
    "dbus-daemon": ("dbus-daemon",),
    "cmake": ("cmake",),
    "extra-cmake-modules": ("extra-cmake-modules",),
    "gettext": ("gettext",),
    "kwin-dev": ("kwin-devel",),
    "kwin-wayland": ("kwin",),
    "libdrm-dev": ("libdrm-devel",),
    "libepoxy-dev": ("libepoxy-devel",),
    "libegl-mesa0": ("libglvnd-egl",),
    "libgl1-mesa-dri": ("mesa-dri-drivers",),
    "libgles2": ("libglvnd-gles",),
    "libkf6config-dev": ("kf6-kconfig-devel",),
    "libkf6configwidgets-dev": ("kf6-kconfigwidgets-devel",),
    "libkf6coreaddons-dev": ("kf6-kcoreaddons-devel",),
    "libkf6i18n-dev": ("kf6-ki18n-devel",),
    "libkf6kcmutils-dev": ("kf6-kcmutils-devel",),
    "libwayland-dev": ("wayland-devel",),
    "libwayland-bin": ("wayland-devel",),
    "libxcb-composite0-dev": ("libxcb-devel",),
    "libxcb-randr0-dev": ("libxcb-devel",),
    "libxcb-res0-dev": ("libxcb-devel",),
    "libxcb-shm0-dev": ("libxcb-devel",),
    "libxcb-sync-dev": ("libxcb-devel",),
    "xwayland": ("xorg-x11-server-Xwayland",),
    "libxkbcommon-dev": ("libxkbcommon-devel",),
    "ninja-build": ("ninja-build",),
    "pkgconf": ("pkgconf-pkg-config",),
    "qt6-base-dev": ("qt6-qtbase-devel",),
    "wayland-protocols": ("wayland-protocols-devel",),
}

# openSUSE capitalises the Mesa packages and versions the KDE ones, so kwin6
# rather than kwin. Its wayland-scanner lives in wayland-devel as on Fedora.
OPENSUSE = {
    "debhelper-compat": (),
    "dbus-daemon": ("dbus-1",),
    "cmake": ("cmake",),
    "extra-cmake-modules": ("kf6-extra-cmake-modules",),
    "gettext": ("gettext-tools",),
    "kwin-dev": ("kwin6-devel",),
    "kwin-wayland": ("kwin6",),
    "libdrm-dev": ("libdrm-devel",),
    "libepoxy-dev": ("libepoxy-devel",),
    "libegl-mesa0": ("Mesa-libEGL-devel",),
    "libgl1-mesa-dri": ("Mesa-dri",),
    "libgles2": ("Mesa-libGLESv3-devel",),
    "libkf6config-dev": ("kf6-kconfig-devel",),
    "libkf6configwidgets-dev": ("kf6-kconfigwidgets-devel",),
    "libkf6coreaddons-dev": ("kf6-kcoreaddons-devel",),
    "libkf6i18n-dev": ("kf6-ki18n-devel",),
    "libkf6kcmutils-dev": ("kf6-kcmutils-devel",),
    "libwayland-dev": ("wayland-devel",),
    "libwayland-bin": ("wayland-devel",),
    "libxcb-composite0-dev": ("libxcb-devel",),
    "libxcb-randr0-dev": ("libxcb-devel",),
    "libxcb-res0-dev": ("libxcb-devel",),
    "libxcb-shm0-dev": ("libxcb-devel",),
    "libxcb-sync-dev": ("libxcb-devel",),
    "xwayland": ("xwayland",),
    "libxkbcommon-dev": ("libxkbcommon-devel",),
    "ninja-build": ("ninja",),
    "pkgconf": ("pkgconf",),
    "qt6-base-dev": ("qt6-base-devel",),
    "wayland-protocols": ("wayland-protocols-devel",),
}

# Arch ships headers with the library, so nearly everything here is the plain
# library name, and KWin's effect headers come from the kwin package itself.
ARCH = {
    "debhelper-compat": (),
    "dbus-daemon": ("dbus",),
    "cmake": ("cmake",),
    "extra-cmake-modules": ("extra-cmake-modules",),
    "gettext": ("gettext",),
    "kwin-dev": ("kwin",),
    "kwin-wayland": ("kwin",),
    "libdrm-dev": ("libdrm",),
    "libepoxy-dev": ("libepoxy",),
    "libegl-mesa0": ("mesa",),
    "libgl1-mesa-dri": ("mesa",),
    "libgles2": ("mesa",),
    "libkf6config-dev": ("kconfig",),
    "libkf6configwidgets-dev": ("kconfigwidgets",),
    "libkf6coreaddons-dev": ("kcoreaddons",),
    "libkf6i18n-dev": ("ki18n",),
    "libkf6kcmutils-dev": ("kcmutils",),
    "libwayland-dev": ("wayland",),
    "libwayland-bin": ("wayland",),
    "libxcb-composite0-dev": ("libxcb",),
    "libxcb-randr0-dev": ("libxcb",),
    "libxcb-res0-dev": ("libxcb",),
    "libxcb-shm0-dev": ("libxcb",),
    "libxcb-sync-dev": ("libxcb",),
    "xwayland": ("xorg-xwayland",),
    "libxkbcommon-dev": ("libxkbcommon",),
    "ninja-build": ("ninja",),
    "pkgconf": ("pkgconf",),
    "qt6-base-dev": ("qt6-base",),
    "wayland-protocols": ("wayland-protocols",),
}

DISTRIBUTIONS = {
    "freebsd": FREEBSD,
    "fedora": FEDORA,
    "opensuse": OPENSUSE,
    "arch": ARCH,
}


def build_depends(control: str) -> set[str]:
    """Return the bare dependency names, dropping versions and build profiles."""
    match = re.search(r"Build-Depends:([^\n]*(?:\n[ \t]+[^\n]*)*)", control)
    if match is None:
        message = "Missing Build-Depends in debian/control"
        raise ValueError(message)
    return {entry.split()[0] for entry in match[1].split(",") if entry.strip()}


def translate(control: str, distribution: str) -> list[str]:
    """Return one distribution's packages, rejecting any unmapped Debian name."""
    packages = DISTRIBUTIONS.get(distribution)
    if packages is None:
        known = ", ".join(sorted(DISTRIBUTIONS))
        message = f"Unknown distribution {distribution!r}; expected one of {known}"
        raise ValueError(message)
    dependencies = build_depends(control)
    # Naming the file to edit matters more here than elsewhere: this runs in a
    # nightly container or virtual machine, where nobody is watching.
    unknown = sorted(dependencies - packages.keys())
    if unknown:
        message = (
            f"debian/control lists {', '.join(unknown)} without a {distribution} package name. "
            "Add the translation to tools/distribution_packages.py."
        )
        raise ValueError(message)
    return sorted({name for entry in dependencies for name in packages[entry]})
