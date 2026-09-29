# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that every package machine boots its own architecture's image."""

import runpy
import unittest
from pathlib import Path

from virtual_machine import emulator

SYSTEMS = runpy.run_path(str(Path(__file__).with_name("package-vm.py")))["SYSTEMS"]


class ArchitectureTest(unittest.TestCase):
    """amd64 under KVM, arm64 emulated, each from an image of its own."""

    def test_amd64_runs_under_kvm(self) -> None:
        """The host's own architecture is run by the host's processor."""
        command = emulator(SYSTEMS["debian-13-amd64"])
        self.assertEqual(command[0], "qemu-system-x86_64")
        self.assertIn("-enable-kvm", command)

    def test_arm64_is_emulated_whole(self) -> None:
        """No KVM and a firmware to boot from, and a screen the virt machine has."""
        machine = SYSTEMS["fedora-43-arm64"]
        command = emulator(machine)
        self.assertEqual(command[0], "qemu-system-aarch64")
        self.assertNotIn("-enable-kvm", command)
        self.assertIn("-bios", command)
        self.assertIn("virtio-gpu-pci,xres=3840,yres=2160", machine.display)

    def test_every_machine_takes_an_image_of_its_architecture(self) -> None:
        """An arm64 machine never boots an amd64 image, and each has a name of its own."""
        for system, machine in SYSTEMS.items():
            architecture = system.rsplit("-", 1)[1]
            with self.subTest(system=system):
                self.assertEqual(machine.architecture, architecture)
                self.assertEqual(machine.name, f"package-{system}")
                marks = ("arm64", "aarch64") if architecture == "arm64" else ("amd64", "x86_64")
                self.assertTrue(any(mark in machine.base for mark in marks), machine.base)


if __name__ == "__main__":
    unittest.main()
