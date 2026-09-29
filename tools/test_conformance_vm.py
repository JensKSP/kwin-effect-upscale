# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what the conformance machine is seeded with and how it is reached.

Making and booting a machine is exercised by doing it; see doc/checks.md.
"""

import runpy
import shlex
import unittest
from pathlib import Path

import yaml
from virtual_machine import ssh, user_data

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


if __name__ == "__main__":
    unittest.main()
