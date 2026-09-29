# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what the conformance machine is seeded with and how it is reached.

Making and booting a machine is exercised by doing it; see doc/checks.md.
"""

import runpy
import shlex
import subprocess
import time
import unittest
from pathlib import Path
from unittest import mock

import yaml
from virtual_machine import (
    CLOUD_INIT_RECOVERABLE,
    expected_sum,
    reached,
    ssh,
    user_data,
    wait_for_first_boot,
)

SCRIPT = runpy.run_path(str(Path(__file__).with_name("conformance-vm.py")))
in_test_image = SCRIPT["in_test_image"]
CONFORMANCE = SCRIPT["CONFORMANCE"]
TEMPLATE = (Path(__file__).resolve().parent.parent / "containers/vm-host/user-data.in").read_text()
# A text of several lines where the machine's host key goes; no key at all,
# since what is checked is that every line arrives, indented, and nothing else.
PRIVATE = "first line of the host key\nsecond line\nlast line\n"


class UserDataTest(unittest.TestCase):
    """The template, filled, is the cloud-config the guest needs."""

    def setUp(self) -> None:
        """Fill the template in the tree with stand-in keys."""
        self.text = user_data(
            TEMPLATE, "ssh-ed25519 AAAAclient tester\n", PRIVATE, "ssh-ed25519 AAAAhost"
        )
        self.config = yaml.safe_load(self.text)

    def test_is_cloud_config(self) -> None:
        """cloud-init reads only a document that says what it is first."""
        self.assertTrue(self.text.startswith("#cloud-config\n"))

    def test_keys_arrive_whole(self) -> None:
        """The login key, and the host key the first connection checks against."""
        self.assertEqual(
            self.config["users"][0]["ssh_authorized_keys"], ["ssh-ed25519 AAAAclient tester"]
        )
        self.assertEqual(self.config["ssh_keys"]["ed25519_private"], PRIVATE)
        self.assertEqual(self.config["ssh_keys"]["ed25519_public"], "ssh-ed25519 AAAAhost")
        # Only the type given, which is there, so the one host key known_hosts
        # names is the only one the guest has.
        self.assertEqual(self.config["ssh_genkeytypes"], ["ed25519"])

    def test_render_device_and_share(self) -> None:
        """Vgem at every boot, and the repository at /src."""
        files = {entry["path"]: entry for entry in self.config["write_files"]}
        self.assertEqual(files["/etc/modules-load.d/vgem.conf"]["content"], "vgem\n")
        self.assertTrue(files["/etc/fstab"]["append"])
        self.assertTrue(files["/etc/fstab"]["content"].startswith("src /src 9p "))
        self.assertIn("render", self.config["users"][0]["groups"])

    def test_no_field_left(self) -> None:
        """A template field nobody filled is refused rather than seeded."""
        with self.assertRaisesRegex(ValueError, "@EXTRA@"):
            user_data(TEMPLATE + "@EXTRA@\n", "key", PRIVATE, "key")


class SumsTest(unittest.TestCase):
    """An image is checked against the line its sums file has for it."""

    def test_both_ways_of_writing_a_sum(self) -> None:
        """Debian writes the name after two spaces, Ubuntu after an asterisk."""
        debian = "aa11  debian-13-generic-amd64.qcow2\nbb22  debian-13-genericcloud-amd64.qcow2\n"
        ubuntu = (
            "cc33 *resolute-server-cloudimg-amd64.img\ndd44 *resolute-server-cloudimg-arm64.img\n"
        )
        self.assertEqual(expected_sum(debian, "debian-13-generic-amd64.qcow2"), "aa11")
        self.assertEqual(expected_sum(ubuntu, "resolute-server-cloudimg-amd64.img"), "cc33")

    def test_the_tagged_form(self) -> None:
        """Fedora names the algorithm and puts the name in parentheses."""
        fedora = (
            "# Fedora-Cloud-43-1.6-x86_64-CHECKSUM\n"
            "SHA256 (Fedora-Cloud-Base-GCE-43-1.6.x86_64.tar.gz) = aa11\n"
            "SHA256 (Fedora-Cloud-Base-Generic-43-1.6.x86_64.qcow2) = bb22\n"
        )
        self.assertEqual(
            expected_sum(fedora, "Fedora-Cloud-Base-Generic-43-1.6.x86_64.qcow2"), "bb22"
        )

    def test_a_name_only_contained_is_no_match(self) -> None:
        """A longer name that contains the one asked for is another file."""
        with self.assertRaisesRegex(ValueError, "not in the sums file"):
            expected_sum(
                "aa11  debian-13-generic-amd64.qcow2.tar\n", "debian-13-generic-amd64.qcow2"
            )


class GuestCommandTest(unittest.TestCase):
    """A command reaches the guest checked, and in one piece."""

    def test_the_host_key_is_checked(self) -> None:
        """Never trust on first use: the machine's known host key or nothing."""
        command = ssh(CONFORMANCE, "true")
        self.assertIn("StrictHostKeyChecking=yes", command)
        self.assertIn("UserKnownHostsFile=/src/build/conformance-vm/known_hosts", command)
        self.assertIn("BatchMode=yes", command)

    def test_words_keep_their_boundaries(self) -> None:
        """The remote shell sees the words it was given, spaces and all."""
        words = ["sh", "-c", "echo 'a b' \"$HOME\""]
        self.assertEqual(shlex.split(ssh(CONFORMANCE, *words)[-1]), words)

    def test_the_test_image_gets_vgem_alone(self) -> None:
        """Only vgem's nodes and the groups that may open them go into the container."""
        remote = shlex.split(in_test_image("true")[-1])
        script = remote[2]
        self.assertIn('*/vgem) devices="$devices --device /dev/dri/${node##*/}"', script)
        self.assertIn("--group-add keep-groups", script)
        self.assertEqual(remote[-2:], ["localhost/upscale-wayland-tests:trixie", "true"])


class DeadlineTest(unittest.TestCase):
    """A wait for the guest ends with the boot's time, whatever the guest does."""

    def test_a_hanging_command_ends_the_wait(self) -> None:
        """A command that never returns is cut off at the deadline, and is no success."""
        hanging = mock.Mock(side_effect=subprocess.TimeoutExpired("ssh", 1))
        with mock.patch("subprocess.run", hanging):
            self.assertFalse(reached(CONFORMANCE, time.monotonic() + 30, "true"))
        self.assertLessEqual(hanging.call_args.kwargs["timeout"], 30)

    def test_a_command_that_succeeds_ends_it_too(self) -> None:
        """The first success is the answer, with no pause before it."""
        answered = mock.Mock(return_value=subprocess.CompletedProcess([], 0))
        with mock.patch("subprocess.run", answered), mock.patch("time.sleep") as pause:
            self.assertTrue(reached(CONFORMANCE, time.monotonic() + 30, "true"))
        pause.assert_not_called()

    def test_cloud_init_diagnostic_is_bounded_and_best_effort(self) -> None:
        """Warnings and a stuck diagnostic cannot prevent the final readiness check."""
        for result in (CLOUD_INIT_RECOVERABLE, subprocess.TimeoutExpired("ssh", 1)):
            with self.subTest(result=result):
                diagnostic = mock.Mock(
                    side_effect=result if isinstance(result, Exception) else None,
                    return_value=subprocess.CompletedProcess([], CLOUD_INIT_RECOVERABLE),
                )
                with (
                    mock.patch("virtual_machine.reached", return_value=True) as ready,
                    mock.patch(
                        "virtual_machine.cloud_init_done", return_value=CLOUD_INIT_RECOVERABLE
                    ),
                    mock.patch("subprocess.run", diagnostic),
                ):
                    wait_for_first_boot(CONFORMANCE, time.monotonic() + 30)
                self.assertEqual(ready.call_count, 2)
                self.assertGreater(diagnostic.call_args.kwargs["timeout"], 0)
                self.assertLessEqual(diagnostic.call_args.kwargs["timeout"], 30)


if __name__ == "__main__":
    unittest.main()
