# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the build dependency translation."""

import unittest
from pathlib import Path

from distribution_packages import DISTRIBUTIONS, build_depends, translate

CONTROL = Path(__file__).resolve().parents[1] / "debian" / "control"
CONTROL_HEADER = "Source: kwin-effect-upscale\n"


class DistributionPackagesTest(unittest.TestCase):
    """Keep every translation complete without booting the target machine."""

    def test_repository_control_is_fully_translated_everywhere(self) -> None:
        """A dependency added without a mapping only fails in the nightly."""
        control = CONTROL.read_text()
        for distribution in DISTRIBUTIONS:
            with self.subTest(distribution=distribution):
                self.assertNotEqual(translate(control, distribution), [])

    def test_every_distribution_maps_the_same_debian_names(self) -> None:
        """Catch a name mapped for one distribution and forgotten for another."""
        names = {distribution: set(table) for distribution, table in DISTRIBUTIONS.items()}
        for distribution, mapped in names.items():
            with self.subTest(distribution=distribution):
                self.assertEqual(mapped, set(DISTRIBUTIONS["freebsd"]))

    def test_control_dependencies_are_all_mapped(self) -> None:
        """The table must cover debian/control exactly, with nothing stale."""
        for distribution, table in DISTRIBUTIONS.items():
            with self.subTest(distribution=distribution):
                self.assertEqual(set(table), build_depends(CONTROL.read_text()))

    def test_unknown_name_names_itself_and_the_file_to_edit(self) -> None:
        """A nightly container's log is the only account of this failure."""
        control = CONTROL_HEADER + "Build-Depends: cmake,\n               libinvented-dev,\n"
        for distribution in DISTRIBUTIONS:
            with self.subTest(distribution=distribution):
                with self.assertRaises(ValueError) as failure:
                    translate(control, distribution)
                self.assertIn("libinvented-dev", str(failure.exception))
                self.assertIn(distribution, str(failure.exception))
                self.assertIn("tools/distribution_packages.py", str(failure.exception))

    def test_unknown_distribution_is_rejected(self) -> None:
        """A typo in a workflow must not translate to an empty install."""
        with self.assertRaises(ValueError) as failure:
            translate(CONTROL.read_text(), "debian")
        self.assertIn("debian", str(failure.exception))

    def test_missing_build_depends_is_rejected(self) -> None:
        """An unparsable control file must not translate to an empty install."""
        for distribution in DISTRIBUTIONS:
            with self.subTest(distribution=distribution), self.assertRaises(ValueError):
                translate(CONTROL_HEADER, distribution)

    def test_version_and_profile_qualifiers_are_ignored(self) -> None:
        """debian/control qualifies names with versions and build profiles."""
        control = (
            CONTROL_HEADER
            + "Build-Depends: cmake (>= 3.24),\n               dbus-daemon <!nocheck>,\n"
        )
        self.assertEqual(translate(control, "freebsd"), ["cmake", "dbus"])
        self.assertEqual(translate(control, "fedora"), ["cmake", "dbus-daemon"])
        self.assertEqual(translate(control, "arch"), ["cmake", "dbus"])

    def test_kwin_development_files_are_mapped_for_every_distribution(self) -> None:
        """Pin the one name whose mistake is guaranteed to waste a nightly.

        Without KWin's development files the effect cannot configure at all.
        """
        control = CONTROL_HEADER + "Build-Depends: kwin-dev,\n"
        expected = {
            "freebsd": ["plasma6-kwin"],
            "fedora": ["kwin-devel"],
            "opensuse": ["kwin6-devel"],
            "arch": ["kwin"],
        }
        for distribution, packages in expected.items():
            with self.subTest(distribution=distribution):
                self.assertEqual(translate(control, distribution), packages)


if __name__ == "__main__":
    unittest.main()
