# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the single list of package targets."""

import unittest

import ci_targets
import release_assets
from ci_targets import TARGETS, matrix, native_architecture, target


class CiTargetsTest(unittest.TestCase):
    """A target that is built but not published is the failure to prevent."""

    def test_every_target_is_named_and_buildable(self) -> None:
        """An empty field would reach a workflow as an empty matrix value."""
        for entry in TARGETS:
            with self.subTest(target=entry.identifier):
                self.assertTrue(entry.label)
                self.assertTrue(entry.family)
                self.assertTrue(entry.architectures)
                self.assertTrue(bool(entry.image) != bool(entry.release))

    def test_only_debian_is_built_twice(self) -> None:
        """The comparison is the reproducibility test, not a second build."""
        twice = {entry.identifier for entry in TARGETS if entry.reproducible}
        self.assertEqual(twice, {"trixie"})

    def test_architecture_names_follow_the_packaging_family(self) -> None:
        """A release inventory that expects amd64 from an RPM finds nothing."""
        self.assertEqual(native_architecture("trixie", "arm64"), "arm64")
        self.assertEqual(native_architecture("fedora", "arm64"), "aarch64")
        self.assertEqual(native_architecture("arch", "amd64"), "x86_64")
        self.assertEqual(native_architecture("freebsd", "amd64"), "amd64")
        with self.assertRaises(KeyError):
            native_architecture("arch", "arm64")

    def test_unknown_targets_fail_loudly(self) -> None:
        """A typo in a workflow input must not silently build nothing."""
        with self.assertRaises(KeyError):
            target("debian")

    def test_matrix_carries_a_runner_for_every_architecture(self) -> None:
        """arm64 packages are built on arm64; nothing here is emulated."""
        for entry in matrix():
            with self.subTest(entry=entry["target"], architecture=entry["architecture"]):
                arm = entry["architecture"] == "arm64"
                self.assertEqual(entry["runner"], "ubuntu-24.04-arm" if arm else "ubuntu-latest")

    def test_virtual_machine_targets_stay_out_of_the_container_matrix(self) -> None:
        """FreeBSD shares no step with a container job and has its own."""
        self.assertNotIn("freebsd", {entry["target"] for entry in matrix()})
        included = {entry["target"] for entry in matrix(container_only=False)}
        self.assertIn("freebsd", included)

    def test_selecting_targets_keeps_their_architectures(self) -> None:
        """The pull request builds one target, not one architecture of it."""
        selected = matrix(["arch"])
        self.assertEqual([entry["architecture"] for entry in selected], ["amd64"])

    def test_release_inventory_reads_the_same_table(self) -> None:
        """Two lists are how a release ships whichever targets succeeded."""
        self.assertEqual(release_assets.DISTRIBUTIONS, ci_targets.DEB_TARGETS)
        for identifier, expected in release_assets.DISTRIBUTION_ARCHITECTURES.items():
            with self.subTest(distribution=identifier):
                self.assertEqual(expected, ci_targets.architectures(identifier))


if __name__ == "__main__":
    unittest.main()
