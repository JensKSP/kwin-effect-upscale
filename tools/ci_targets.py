# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""The only list of package targets: CI matrix, labels, images and inventory.

Every workflow matrix, every container base image and the release inventory
read this table. A distribution is added, renamed or moved to a new release in
one place, because a second list is how a target ends up built but not
published, or published but never built.

The handbook's "which release of each distribution" section explains which
release each target names and why it is the latest stable one.
"""

import argparse
import json
from dataclasses import dataclass

# GitHub's own names, which decide the runner. Each packaging family spells
# them its own way; native_architecture() translates, and nothing else does.
ARCHITECTURES = ("amd64", "arm64")
NATIVE_ARCHITECTURES = {
    "deb": {"amd64": "amd64", "arm64": "arm64"},
    "rpm": {"amd64": "x86_64", "arm64": "aarch64"},
    "arch": {"amd64": "x86_64"},
    "pkg": {"amd64": "amd64"},
}


@dataclass(frozen=True)
class Target:
    """One distribution this project builds a package for."""

    identifier: str
    label: str
    family: str
    container: str
    image: str
    architectures: tuple[str, ...]
    # Built twice from separate clean sources and compared byte for byte. Only
    # Debian carries it: the comparison finds nondeterminism in the build, and
    # one target exercising the build information and the packaging flags is
    # what that costs. Every other target is built once.
    reproducible: bool = False
    # Whether the installed package is tested with the whole suite or with the
    # load test alone. The suite needs a session, so it needs a distribution
    # whose KWin the effect is built against and a host that has been accepted.
    suite: bool = False
    # The FreeBSD release the virtual machine runs. Empty for the container
    # targets, which name a base image instead.
    release: str = ""
    # The name a download link calls this distribution. It carries neither the
    # version nor the distribution's release, because a link in the README has
    # to survive both of them moving. Empty means the identifier already is it.
    alias: str = ""
    # What a clean environment of this target needs before it can run the
    # install test: an interpreter, and nothing else. Keeping it to that is the
    # point of the test, because everything else has to come from the package.
    bootstrap: str = ""
    # What the suite additionally needs to start a nested compositor session.
    # Only a target that runs the suite has any.
    session: str = ""


TARGETS = (
    Target(
        identifier="trixie",
        label="Debian Trixie",
        alias="debian",
        family="deb",
        container="package",
        image="docker.io/library/debian:trixie",
        architectures=ARCHITECTURES,
        reproducible=True,
        suite=True,
        bootstrap="apt-get update && apt-get install --no-install-recommends -y python3",
        session="kwin-wayland xwayland libgl1-mesa-dri libqt6test6 dbus-daemon",
    ),
    Target(
        identifier="resolute",
        label="Kubuntu 26.04 LTS",
        alias="kubuntu",
        family="deb",
        container="package",
        image="docker.io/library/ubuntu:26.04",
        architectures=ARCHITECTURES,
        bootstrap="apt-get update && apt-get install --no-install-recommends -y python3",
    ),
    Target(
        identifier="fedora",
        label="Fedora",
        family="rpm",
        container="fedora",
        image="docker.io/library/fedora:43",
        architectures=ARCHITECTURES,
        bootstrap="dnf install -y --setopt=install_weak_deps=False python3",
    ),
    Target(
        identifier="opensuse",
        label="openSUSE Tumbleweed",
        family="rpm",
        container="opensuse",
        image="docker.io/opensuse/tumbleweed",
        architectures=ARCHITECTURES,
        bootstrap=(
            "zypper -n --gpg-auto-import-keys refresh && zypper -n install --no-recommends python3"
        ),
    ),
    # Arch is x86_64 only, and not as a decision taken here: the official image
    # publishes no arm64, because Arch supports one architecture and the ARM
    # port is a separate distribution with its own repositories.
    Target(
        identifier="arch",
        label="Arch",
        family="arch",
        container="arch",
        image="docker.io/library/archlinux:base-devel",
        architectures=("amd64",),
        bootstrap="pacman -Sy --noconfirm python",
    ),
    # FreeBSD builds in a virtual machine rather than a container, so it has a
    # release instead of an image and its own jobs. GitHub offers no FreeBSD
    # runner for a second architecture, which is why it is amd64 alone.
    Target(
        identifier="freebsd",
        label="FreeBSD",
        family="pkg",
        container="",
        image="",
        architectures=("amd64",),
        release="15.0",
        bootstrap="pkg install -y python3",
    ),
)

BY_IDENTIFIER = {target.identifier: target for target in TARGETS}
# The Debian family is the one the release inventory can enumerate by name:
# every other family stamps its own release, distribution or architecture into
# the file name. See release_assets.py.
DEB_TARGETS = tuple(target.identifier for target in TARGETS if target.family == "deb")


def target(identifier: str) -> Target:
    """Fail on an unknown target rather than silently building nothing."""
    if identifier not in BY_IDENTIFIER:
        message = f"Unknown target {identifier!r}; known: {', '.join(BY_IDENTIFIER)}"
        raise KeyError(message)
    return BY_IDENTIFIER[identifier]


def native_architecture(identifier: str, architecture: str) -> str:
    """Translate a matrix architecture into what that packaging family calls it."""
    names = NATIVE_ARCHITECTURES[target(identifier).family]
    if architecture not in names:
        message = f"{identifier} is not built for {architecture}"
        raise KeyError(message)
    return names[architecture]


def architectures(identifier: str) -> set[str]:
    """Name the native architectures a complete release carries for a target."""
    chosen = target(identifier)
    return {native_architecture(identifier, name) for name in chosen.architectures}


# What a stable download name ends in, per packaging family.
DOWNLOAD_SUFFIXES = {"deb": ".deb", "rpm": ".rpm", "arch": ".pkg.tar.zst", "pkg": ".pkg"}


def download_name(identifier: str, architecture: str) -> str:
    """Name the copy a download link points at, unchanged from build to build.

    The published package carries the version and, for the Debian family, the
    distribution's release, so its name is different every night. This is the
    second name each installable package is published under, so that one link
    in the README keeps working across versions and across a distribution
    upgrade. Only the package a person installs gets one; debug symbols,
    source packages and build records keep their own names alone.
    """
    entry = target(identifier)
    name = entry.alias or entry.identifier
    native = native_architecture(identifier, architecture)
    return f"kwin-effect-upscale-{name}-{native}{DOWNLOAD_SUFFIXES[entry.family]}"


def runner(architecture: str) -> str:
    """Hosted runner for an architecture; arm64 packages need an arm64 host."""
    return "ubuntu-24.04-arm" if architecture == "arm64" else "ubuntu-latest"


def matrix(
    identifiers: list[str] | None = None,
    *,
    container_only: bool = True,
    architectures: list[str] | None = None,
) -> list[dict[str, object]]:
    """One entry per job, carrying everything a job needs and nothing it does not."""
    chosen = [target(name) for name in identifiers] if identifiers else list(TARGETS)
    wanted = set(architectures) if architectures else set(ARCHITECTURES)
    return [
        {
            "target": entry.identifier,
            "label": entry.label,
            "family": entry.family,
            "container": entry.container,
            "image": entry.image,
            "release": entry.release,
            "architecture": architecture,
            "runner": runner(architecture),
            "reproducible": entry.reproducible,
            "suite": entry.suite,
            "bootstrap": entry.bootstrap,
            "session": entry.session,
        }
        for entry in chosen
        if not (container_only and not entry.container)
        for architecture in entry.architectures
        if architecture in wanted
    ]


def main() -> None:
    """Print a matrix for a workflow, as a single line for GITHUB_OUTPUT."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", default="", help="comma separated; empty means all")
    parser.add_argument(
        "--architectures",
        default="",
        help="comma separated; empty means every architecture the target is built for",
    )
    machines = parser.add_mutually_exclusive_group()
    machines.add_argument(
        "--include-virtual-machines",
        action="store_true",
        help="include targets built in a virtual machine rather than a container",
    )
    machines.add_argument(
        "--only-virtual-machines",
        action="store_true",
        help="only those targets, which have their own jobs and share no step",
    )
    arguments = parser.parse_args()
    names = [name for name in arguments.targets.split(",") if name]
    architectures = [name for name in arguments.architectures.split(",") if name]
    entries = matrix(
        names or None,
        container_only=not (arguments.include_virtual_machines or arguments.only_virtual_machines),
        architectures=architectures or None,
    )
    if arguments.only_virtual_machines:
        entries = [entry for entry in entries if not entry["container"]]
    print(json.dumps({"include": entries}, separators=(",", ":")))


if __name__ == "__main__":
    main()
