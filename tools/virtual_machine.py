# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Make, start, stop and reach the virtual machines the checks run in.

Each machine is a profile: a Debian cloud image, checked against Debian's
SHA512SUMS, with a disk of its own on top; a seed from a cloud-init template
under containers/vm-host, filled with a login key and a host key made for that
machine alone, so that known_hosts is written before the first boot; and QEMU
under KVM in containers/vm-host, given /dev/kvm and the repository, which the
guest mounts at /src, and nothing else of the host. Everything of a machine
lives under build/<name>. tools/conformance-vm.py and tools/package-vm.py are
the commands; this is what they share.
"""

from __future__ import annotations

import datetime
import hashlib
import re
import shlex
import subprocess
import sys
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST_IMAGE = "localhost/upscale-vm-host:trixie"
CLOUD = "https://cloud.debian.org/images/cloud/trixie/latest/"
# The generic image, with Debian's standard kernel: the cloud kernel of the
# genericcloud image has no 9p, which the repository's share needs (2026-09-29).
BASE = "debian-13-generic-amd64.qcow2"
SSH_PORT = 2222
# What makes one machine: removed by --replace. The downloaded base stays.
MACHINE_FILES = (
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


@dataclass(frozen=True)
class Machine:
    """One kind of virtual machine and what it takes to have one."""

    name: str
    template: str
    # A shell command the guest runs once cloud-init is done; the machine is of
    # use only when it succeeds.
    ready: str
    # QEMU's display device. With -vga none the genericcloud image reset the
    # machine before its kernel printed a line (QEMU 10.0.13, 2026-09-29), so
    # there always is one.
    display: tuple[str, ...] = ("-display", "none")
    cpus: int = 6
    memory: int = 8
    disk: str = "64G"
    # How long the first boot may take, installation of the template's
    # packages included.
    boot_seconds: int = 900

    @property
    def directory(self) -> Path:
        """Where the machine's files live."""
        return ROOT / "build" / self.name

    @property
    def shared(self) -> str:
        """The same directory, as the containers and the guest see it."""
        return f"/src/build/{self.name}"

    @property
    def container(self) -> str:
        """The container QEMU runs in."""
        return f"upscale-{self.name}"


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


def ssh(machine: Machine, *command: str) -> list[str]:
    """Name the command that runs a command in a guest, through its container."""
    return [
        *("podman", "exec", machine.container, "ssh", "-F", "/dev/null"),
        *("-i", f"{machine.shared}/id_ed25519"),
        *("-o", f"UserKnownHostsFile={machine.shared}/known_hosts"),
        *("-o", "StrictHostKeyChecking=yes", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5"),
        *("-o", "LogLevel=ERROR", "-p", str(SSH_PORT), "tester@127.0.0.1"),
        shlex.join(command),
    ]


def state(machine: Machine) -> str:
    """Say whether a machine's container runs, has stopped, or does not exist."""
    found = subprocess.run(
        ["podman", "container", "inspect", "--format", "{{.State.Status}}", machine.container],
        check=False,
        capture_output=True,
        text=True,
    )
    return found.stdout.strip() if found.returncode == 0 else "absent"


def user_data(template: str, authorized: str, private: str, public: str) -> str:
    """Fill a cloud-init template with the keys made for one machine."""
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


def download(directory: Path) -> None:
    """Fetch Debian's current cloud image, unless the one here is still it."""
    # A constant https address, not something a caller chose.
    with urllib.request.urlopen(CLOUD + "SHA512SUMS", timeout=60) as response:  # noqa: S310  # nosec B310
        sums = response.read().decode()
    expected = next(line.split()[0] for line in sums.splitlines() if line.endswith(" " + BASE))
    base = directory / BASE
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


def create(machine: Machine, replace: bool) -> None:  # noqa: FBT001 - One command line switch.
    """Make a new machine with keys of its own, and start it."""
    directory = machine.directory
    if (directory / "disk.qcow2").exists():
        if not replace:
            sys.exit("A machine exists already; --replace makes a new one in its place.")
        if state(machine) == "running":
            sys.exit("The machine is running; stop it first.")
        for name in MACHINE_FILES:
            (directory / name).unlink(missing_ok=True)
    directory.mkdir(parents=True, exist_ok=True)
    # From the recipe in the tree every time, never from whatever image has the
    # name: a new machine is a statement about what the repository makes.
    run("podman", "build", "--pull", "-t", HOST_IMAGE, "containers/vm-host")
    download(directory)
    for key in ("id_ed25519", "host_ed25519"):
        in_container(
            HOST_IMAGE,
            *("ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", f"upscale-{machine.name} {key}"),
            *("-f", f"{machine.shared}/{key}"),
        )
    host = (directory / "host_ed25519.pub").read_text().split()
    (directory / "user-data").write_text(
        user_data(
            (ROOT / machine.template).read_text(),
            (directory / "id_ed25519.pub").read_text(),
            (directory / "host_ed25519").read_text(),
            " ".join(host[:2]),
        )
    )
    stamp = datetime.datetime.now(datetime.UTC).strftime("%Y%m%d%H%M%S")
    (directory / "meta-data").write_text(
        f"instance-id: upscale-{machine.name}-{stamp}\nlocal-hostname: upscale-{machine.name}\n"
    )
    (directory / "known_hosts").write_text(f"[127.0.0.1]:{SSH_PORT} {host[0]} {host[1]}\n")
    seed = (f"{machine.shared}/seed.img", f"{machine.shared}/user-data")
    in_container(HOST_IMAGE, "cloud-localds", *seed, f"{machine.shared}/meta-data")
    # The disk records its base by a name relative to itself.
    in_container(
        HOST_IMAGE,
        *("qemu-img", "create", "-q", "-f", "qcow2", "-F", "qcow2", "-b", BASE),
        *(f"{machine.shared}/disk.qcow2", machine.disk),
    )
    start(machine)


def start(machine: Machine) -> None:
    """Boot a machine and wait until its first boot has finished."""
    current = state(machine)
    if current == "running":
        print("The machine is running.")
        return
    if current != "absent":
        run("podman", "rm", machine.container)
    shared = machine.shared
    # /dev/kvm belongs to the kvm group, which a rootless container drops
    # unless it keeps the groups of the user who starts it. QEMU runs as that
    # user too: the share tells the guest the owners QEMU sees, and the guest's
    # tester, uid 1000, can then write where its host counterpart can.
    run(
        *("podman", "run", "-d", "--name", machine.container, "--device", "/dev/kvm"),
        *("--group-add", "keep-groups", "--userns", "keep-id"),
        *("-v", f"{ROOT}:/src", HOST_IMAGE, "qemu-system-x86_64", "-enable-kvm", "-cpu", "host"),
        *("-smp", str(machine.cpus), "-m", f"{machine.memory}G", *machine.display),
        *("-no-reboot", "-serial", f"file:{shared}/console.log"),
        *("-drive", f"file={shared}/disk.qcow2,if=virtio,format=qcow2"),
        *("-drive", f"file={shared}/seed.img,if=virtio,format=raw"),
        *("-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{SSH_PORT}-:22"),
        *("-device", "virtio-net-pci,netdev=net0"),
        *("-virtfs", "local,path=/src,mount_tag=src,security_model=none,id=src"),
        capture=True,
    )
    deadline = time.monotonic() + machine.boot_seconds
    while subprocess.run(ssh(machine, "true"), check=False, capture_output=True).returncode:
        if state(machine) != "running" or time.monotonic() > deadline:
            sys.exit(f"The machine did not come up; see {machine.directory / 'console.log'}.")
        time.sleep(5)
    # A degraded first boot is an error here: the guest is only of use whole.
    run(*ssh(machine, "cloud-init", "status", "--wait"))
    run(*ssh(machine, "sh", "-c", f"mountpoint -q /src && {machine.ready}"))
    print("The machine is up.")


def stop(machine: Machine) -> None:
    """Shut a guest down and remove its container."""
    if state(machine) == "running":
        subprocess.run(ssh(machine, "sudo", "systemctl", "poweroff"), check=False)
        try:
            subprocess.run(["podman", "wait", machine.container], check=False, timeout=180)
        except subprocess.TimeoutExpired:
            run("podman", "stop", "--time", "10", machine.container)
    if state(machine) != "absent":
        run("podman", "rm", machine.container)
