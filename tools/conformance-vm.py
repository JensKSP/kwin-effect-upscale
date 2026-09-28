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
it is done with.

A compositor under test needs a DRM render device, and on a developer's machine
the only one is the desktop's own GPU: several test compositors on it froze the
desktop on 2026-09-27. The guest has none of the host's devices. It loads vgem,
on which Mesa's llvmpipe draws. QEMU runs in containers/vm-host under KVM,
which is all it is given of the host besides the repository, which the guest
mounts at /src as the containers do.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import re
import shlex
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIRECTORY = ROOT / "build" / "conformance-vm"
# The same directory as the containers and the guest see the repository.
SHARED = "/src/build/conformance-vm"
HOST_IMAGE = "localhost/upscale-vm-host:trixie"
TEST_IMAGE = "localhost/upscale-wayland-tests:trixie"
CONTAINER = "upscale-conformance-vm"
CLOUD = "https://cloud.debian.org/images/cloud/trixie/latest/"
# The generic image, with Debian's standard kernel: the cloud kernel of the
# genericcloud image has no 9p, which the repository's share needs (2026-09-29).
BASE = "debian-13-generic-amd64.qcow2"
SSH_PORT = 2222
# What makes one machine: removed by --replace. The downloaded base stays.
MACHINE = (
    "disk.qcow2",
    "seed.img",
    "user-data",
    "meta-data",
    "known_hosts",
    "id_ed25519",
    "id_ed25519.pub",
    "host_ed25519",
    "host_ed25519.pub",
    "console.log",
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


def run(*command: str, capture: bool = False, check: bool = True) -> str:
    """Run a command from the repository root and return what it printed."""
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=check,
        text=True,
        stdout=subprocess.PIPE if capture else None,
    )
    return result.stdout or ""


def in_container(image: str, *command: str) -> None:
    """Run a command in a throwaway container of an image, with the repository."""
    run("podman", "run", "--rm", "-v", f"{ROOT}:/src", "-w", "/src", image, *command)


def ssh(*command: str) -> list[str]:
    """Name the command that runs a command in the guest, through its container."""
    return [
        *("podman", "exec", CONTAINER, "ssh", "-F", "/dev/null"),
        *("-i", f"{SHARED}/id_ed25519", "-o", f"UserKnownHostsFile={SHARED}/known_hosts"),
        *("-o", "StrictHostKeyChecking=yes", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5"),
        *("-o", "LogLevel=ERROR", "-p", str(SSH_PORT), "tester@127.0.0.1"),
        shlex.join(command),
    ]


def in_test_image(*command: str, image: str = TEST_IMAGE) -> list[str]:
    """Name the command that runs a command in a test image, in the guest."""
    return ssh("sh", "-c", IN_TEST_IMAGE, "sh", image, *command)


def state() -> str:
    """Say whether the machine's container runs, has stopped, or does not exist."""
    found = subprocess.run(
        ["podman", "container", "inspect", "--format", "{{.State.Status}}", CONTAINER],
        check=False,
        capture_output=True,
        text=True,
    )
    return found.stdout.strip() if found.returncode == 0 else "absent"


def user_data(template: str, authorized: str, private: str, public: str) -> str:
    """Fill the cloud-init template with the keys made for one machine."""
    indented = "".join(f"    {line}\n" for line in private.strip().splitlines())
    text = (
        template.replace("@AUTHORIZED_KEY@", authorized.strip())
        .replace("@HOST_KEY_PRIVATE@\n", indented)
        .replace("@HOST_KEY_PUBLIC@", public.strip())
    )
    if left := re.findall(r"@[A-Z_]+@", text):
        message = f"cloud-init template fields left unfilled: {left}"
        raise ValueError(message)
    return text


def checksum(path: Path) -> str:
    """Hash a file as Debian's SHA512SUMS does."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha512").hexdigest()


def download() -> None:
    """Fetch Debian's current cloud image, unless the one here is still it."""
    # A constant https address, not something a caller chose.
    with urllib.request.urlopen(CLOUD + "SHA512SUMS", timeout=60) as response:  # noqa: S310  # nosec B310
        sums = response.read().decode()
    expected = next(line.split()[0] for line in sums.splitlines() if line.endswith(" " + BASE))
    base = DIRECTORY / BASE
    if base.exists() and checksum(base) == expected:
        return
    partial = base.with_suffix(".partial")
    with (
        urllib.request.urlopen(CLOUD + BASE, timeout=60) as response,  # noqa: S310  # nosec B310
        partial.open("wb") as stream,
    ):
        while chunk := response.read(1 << 20):
            stream.write(chunk)
    if checksum(partial) != expected:
        partial.unlink()
        message = f"{BASE} does not match Debian's SHA512SUMS"
        raise ValueError(message)
    partial.replace(base)


def create(replace: bool) -> None:  # noqa: FBT001 - One command line switch.
    """Make a new machine with keys of its own, and start it."""
    if (DIRECTORY / "disk.qcow2").exists():
        if not replace:
            sys.exit("A machine exists already; --replace makes a new one in its place.")
        if state() == "running":
            sys.exit("The machine is running; stop it first.")
        for name in MACHINE:
            (DIRECTORY / name).unlink(missing_ok=True)
    DIRECTORY.mkdir(parents=True, exist_ok=True)
    # From the recipe in the tree every time, never from whatever image has the
    # name: a new machine is a statement about what the repository makes.
    run("podman", "build", "--pull", "-t", HOST_IMAGE, "containers/vm-host")
    download()
    for key in ("id_ed25519", "host_ed25519"):
        in_container(
            HOST_IMAGE,
            *("ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", f"upscale-conformance {key}"),
            *("-f", f"{SHARED}/{key}"),
        )
    host = (DIRECTORY / "host_ed25519.pub").read_text().split()
    (DIRECTORY / "user-data").write_text(
        user_data(
            (ROOT / "containers/vm-host/user-data.in").read_text(),
            (DIRECTORY / "id_ed25519.pub").read_text(),
            (DIRECTORY / "host_ed25519").read_text(),
            " ".join(host[:2]),
        )
    )
    stamp = datetime.datetime.now(datetime.UTC).strftime("%Y%m%d%H%M%S")
    (DIRECTORY / "meta-data").write_text(
        f"instance-id: upscale-conformance-{stamp}\nlocal-hostname: upscale-conformance\n"
    )
    (DIRECTORY / "known_hosts").write_text(f"[127.0.0.1]:{SSH_PORT} {host[0]} {host[1]}\n")
    in_container(
        HOST_IMAGE,
        "cloud-localds",
        f"{SHARED}/seed.img",
        f"{SHARED}/user-data",
        f"{SHARED}/meta-data",
    )
    # The disk records its base by a name relative to itself.
    in_container(
        HOST_IMAGE,
        *("qemu-img", "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", BASE),
        *(f"{SHARED}/disk.qcow2", "64G"),
    )
    start(6, 8)


def start(cpus: int, memory: int) -> None:
    """Boot the machine and wait until its first boot has finished."""
    current = state()
    if current == "running":
        print("The machine is running.")
        return
    if current != "absent":
        run("podman", "rm", CONTAINER)
    # /dev/kvm belongs to the kvm group, which a rootless container drops
    # unless it keeps the groups of the user who starts it. QEMU runs as that
    # user too: the share tells the guest the owners QEMU sees, and the guest's
    # tester, uid 1000, can then write where its host counterpart can.
    run(
        *("podman", "run", "-d", "--name", CONTAINER, "--device", "/dev/kvm"),
        *("--group-add", "keep-groups", "--userns", "keep-id"),
        *("-v", f"{ROOT}:/src", HOST_IMAGE, "qemu-system-x86_64", "-enable-kvm", "-cpu", "host"),
        # With -vga none, the genericcloud image reset the machine before its
        # kernel printed a line (QEMU 10.0.13, 2026-09-29): the display stays.
        *("-smp", str(cpus), "-m", f"{memory}G", "-display", "none"),
        *("-no-reboot", "-serial", f"file:{SHARED}/console.log"),
        *("-drive", f"file={SHARED}/disk.qcow2,if=virtio,format=qcow2"),
        *("-drive", f"file={SHARED}/seed.img,if=virtio,format=raw"),
        *("-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{SSH_PORT}-:22"),
        *("-device", "virtio-net-pci,netdev=net0"),
        *("-virtfs", "local,path=/src,mount_tag=src,security_model=none,id=src"),
        capture=True,
    )
    deadline = time.monotonic() + 900
    while subprocess.run(ssh("true"), check=False, capture_output=True).returncode:
        if state() != "running" or time.monotonic() > deadline:
            sys.exit(f"The machine did not come up; see {DIRECTORY / 'console.log'}.")
        time.sleep(5)
    # A degraded first boot is an error here: the guest is only of use whole.
    run(*ssh("cloud-init", "status", "--wait"))
    vgem = "readlink -f /sys/class/drm/*/device | grep -q /vgem$"
    run(*ssh("sh", "-c", f"mountpoint -q /src && {vgem}"))
    print("The machine is up.")


def stop() -> None:
    """Shut the guest down and remove its container."""
    if state() == "running":
        subprocess.run(ssh("sudo", "systemctl", "poweroff"), check=False)
        try:
            subprocess.run(["podman", "wait", CONTAINER], check=False, timeout=180)
        except subprocess.TimeoutExpired:
            run("podman", "stop", "--time", "10", CONTAINER)
    if state() != "absent":
        run("podman", "rm", CONTAINER)


def load(image: str) -> None:
    """Copy an image into the guest, saving it again only when it changed."""
    identity = run("podman", "image", "inspect", "--format", "{{.Id}}", image, capture=True)
    name = re.sub(r"[^A-Za-z0-9.-]", "_", image.removeprefix("localhost/"))
    archive = DIRECTORY / f"{name}.tar"
    stamp = DIRECTORY / f"{name}.id"
    if not archive.exists() or not stamp.exists() or stamp.read_text() != identity:
        archive.unlink(missing_ok=True)
        run("podman", "save", "--output", str(archive), image)
        stamp.write_text(identity)
    run(*ssh("podman", "load", "--input", f"{SHARED}/{archive.name}"))


def main(argv: list[str] | None = None) -> int:
    """Carry out one command."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("create").add_argument("--replace", action="store_true")
    starting = commands.add_parser("start")
    starting.add_argument("--cpus", type=int, default=6)
    starting.add_argument("--memory", type=int, default=8, help="GiB")
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
        create(arguments.replace)
    elif arguments.command == "start":
        start(arguments.cpus, arguments.memory)
    elif arguments.command == "stop":
        stop()
    elif arguments.command == "status":
        print(state())
    elif arguments.command == "load":
        load(arguments.image)
    elif arguments.command == "prepare-kwin":
        in_container(TEST_IMAGE, "sh", "-c", PREPARE_KWIN)
    elif arguments.command == "production":
        in_container(TEST_IMAGE, "sh", "-c", BUILD_EFFECT)
        words = ("sh", "-c", PRODUCTION, "production", *arguments.cases)
        return subprocess.run(in_test_image(*words), cwd=ROOT, check=False).returncode
    elif arguments.command == "test":
        command = in_test_image(*arguments.words, image=arguments.image)
        return subprocess.run(command, cwd=ROOT, check=False).returncode
    else:
        return subprocess.run(ssh(*arguments.words), cwd=ROOT, check=False).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
