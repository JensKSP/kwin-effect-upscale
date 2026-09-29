#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make, run and throw away the virtual machine the compositor tests run in.

    conformance-vm.py create [--replace]    a fresh machine, started
    conformance-vm.py start | stop | status
    conformance-vm.py guest COMMAND...      a command in the guest
    conformance-vm.py load [IMAGE]          the test image, copied into the guest
    conformance-vm.py prepare-kwin          KWin's own tests, from its packaged source
    conformance-vm.py test COMMAND...       a command in the test image, in the guest
    conformance-vm.py production [CASE...]  KWin's production test with this tree's effect

Everything lives under build/conformance-vm: the Debian cloud image, the
machine's disk on top of it, its seed, and the keys made for it. A machine is
made fresh for each release (Jens, 2026-09-28), and the directory can go when
it is done with. How a machine is made is tools/virtual_machine.py's.

A compositor under test needs a DRM render device, and on a developer's machine
the only one is the desktop's own GPU: several test compositors on it froze the
desktop on 2026-09-27. The guest has none of the host's devices. It loads vgem,
on which Mesa's llvmpipe draws. QEMU runs in containers/vm-host under KVM,
which is all it is given of the host besides the repository, which the guest
mounts at /src as the containers do.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

import virtual_machine as vm

TEST_IMAGE = "localhost/upscale-wayland-tests:trixie"
# The guest loads vgem, its render device: a DRM driver without hardware, on
# which Mesa's llvmpipe draws.
CONFORMANCE = vm.Machine(
    name="conformance-vm",
    template="containers/vm-host/user-data.in",
    ready="readlink -f /sys/class/drm/*/device | grep -q /vgem$",
)
# The guest has QEMU's standard display device beside vgem, whose DRM node KWin
# must not pick, so only vgem's nodes go into the test container, found by the
# device they belong to rather than by their numbers. The groups that may open
# them go with them. The tests run as the guest's own user, as a session does:
# the X11 proxy refuses to run as root, and what they write to the share is
# then the host user's.
IN_TEST_IMAGE = """devices=
for node in /sys/class/drm/*; do
    case $(readlink -f "$node/device") in
        */vgem) devices="$devices --device /dev/dri/${node##*/}" ;;
    esac
done
test -n "$devices"
exec podman run --rm $devices --group-add keep-groups --userns keep-id \\
    -v /src:/src -w /src "$@"
"""
# The test compositor's environment: nothing of a session it might have been
# started from, a home of its own, and the software renderer.
PRODUCTION = """set -e
runtime=$(mktemp -d /tmp/conformance-XXXXXX)
for child in home config data cache empty; do mkdir "$runtime/$child"; done
env -u DISPLAY -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS -u QT_QPA_PLATFORM \\
    HOME="$runtime/home" XDG_RUNTIME_DIR="$runtime" XDG_CONFIG_HOME="$runtime/config" \\
    XDG_DATA_HOME="$runtime/data" XDG_CACHE_HOME="$runtime/cache" \\
    XDG_CONFIG_DIRS="$runtime/empty" UPSCALE_CONFORMANCE_ARM=active \\
    LIBGL_ALWAYS_SOFTWARE=1 GBM_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 LC_ALL=C.UTF-8 \\
    QT_PLUGIN_PATH=/src/build/conformance-vm/effect/bin:/src/build/conformance-vm/kwin-build/bin \\
    QT_LOGGING_RULES=kwin_effect_upscale=true QT_LOGGING_TO_CONSOLE=1 \\
    timeout 600 /src/build/conformance-vm/kwin-build/bin/testUpscaleProduction "$@"
"""
# KWin's packaged source at the version the test image runs, adapted and built;
# only the test this project adds is built.
PREPARE_KWIN = """set -e
version=$(dpkg-query --show --showformat '${Version}' kwin-wayland)
mkdir -p build/conformance-vm/kwin-source
if [ ! -e build/conformance-vm/kwin-tests ]; then
    rm -rf build/conformance-vm/kwin-source/*
    (cd build/conformance-vm/kwin-source && apt-get update && apt-get source "kwin=$version")
    source=$(find build/conformance-vm/kwin-source -mindepth 1 -maxdepth 1 -type d)
    python3 -B tools/prepare-kwin-tests.py --source "$source" --out build/conformance-vm/kwin-tests
else
    python3 -B tools/prepare-kwin-tests.py --source none --out build/conformance-vm/kwin-tests \\
        --refresh
fi
cmake -S build/conformance-vm/kwin-tests -B build/conformance-vm/kwin-build -G Ninja \\
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build build/conformance-vm/kwin-build --target testUpscaleProduction
"""
BUILD_EFFECT = """set -e
cmake -S . -B build/conformance-vm/effect -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \\
    -DBUILD_TESTING=OFF
cmake --build build/conformance-vm/effect
"""


def in_test_image(*command: str, image: str = TEST_IMAGE) -> list[str]:
    """Name the command that runs a command in a test image, in the guest."""
    return vm.ssh(CONFORMANCE, "sh", "-c", IN_TEST_IMAGE, "sh", image, *command)


def load(image: str) -> None:
    """Copy an image into the guest, saving it again only when it changed."""
    identity = vm.run("podman", "image", "inspect", "--format", "{{.Id}}", image, capture=True)
    name = re.sub(r"[^A-Za-z0-9.-]", "_", image.removeprefix("localhost/"))
    archive = CONFORMANCE.directory / f"{name}.tar"
    stamp = CONFORMANCE.directory / f"{name}.id"
    if not archive.exists() or not stamp.exists() or stamp.read_text() != identity:
        archive.unlink(missing_ok=True)
        vm.run("podman", "save", "--output", str(archive), image)
        stamp.write_text(identity)
    vm.run(
        *vm.ssh(CONFORMANCE, "podman", "load", "--input", f"{CONFORMANCE.shared}/{archive.name}")
    )


def main(argv: list[str] | None = None) -> int:
    """Carry out one command."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("create").add_argument("--replace", action="store_true")
    commands.add_parser("start")
    commands.add_parser("stop")
    commands.add_parser("status")
    # Everything after the command's name is the guest's, options included.
    commands.add_parser("guest").add_argument("words", nargs=argparse.REMAINDER)
    commands.add_parser("load").add_argument("image", nargs="?", default=TEST_IMAGE)
    commands.add_parser("prepare-kwin")
    testing = commands.add_parser("test")
    testing.add_argument("--image", default=TEST_IMAGE)
    testing.add_argument("words", nargs=argparse.REMAINDER)
    commands.add_parser("production").add_argument("cases", nargs="*")
    arguments = parser.parse_args(argv)
    if arguments.command == "create":
        vm.create(CONFORMANCE, arguments.replace)
    elif arguments.command == "start":
        vm.start(CONFORMANCE)
    elif arguments.command == "stop":
        vm.stop(CONFORMANCE)
    elif arguments.command == "status":
        print(vm.state(CONFORMANCE))
    elif arguments.command == "load":
        load(arguments.image)
    elif arguments.command == "prepare-kwin":
        vm.in_container(TEST_IMAGE, "sh", "-c", PREPARE_KWIN)
    elif arguments.command == "production":
        vm.in_container(TEST_IMAGE, "sh", "-c", BUILD_EFFECT)
        words = ("sh", "-c", PRODUCTION, "production", *arguments.cases)
        return subprocess.run(in_test_image(*words), cwd=vm.ROOT, check=False).returncode
    elif arguments.command == "test":
        command = in_test_image(*arguments.words, image=arguments.image)
        return subprocess.run(command, cwd=vm.ROOT, check=False).returncode
    else:
        guest = vm.ssh(CONFORMANCE, *arguments.words)
        return subprocess.run(guest, cwd=vm.ROOT, check=False).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
