# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the FreeBSD build dependency translation."""

import runpy
import unittest
from pathlib import Path

CONTROL = Path(__file__).resolve().parents[1] / "debian" / "control"
TRANSLATE = runpy.run_path(str(Path(__file__).with_name("freebsd-packages.py")))["translate"]

CONTROL_HEADER = "Source: kwin-effect-upscale\n"


class FreeBsdPackagesTest(unittest.TestCase):
    """Keep the translation complete without booting a FreeBSD machine."""

    def test_repository_control_is_fully_translated(self) -> None:
        """A build dependency added without a mapping only fails in the nightly."""
        self.assertIn("libxcb", TRANSLATE(CONTROL.read_text()))

    def test_unknown_name_names_itself_and_the_file_to_edit(self) -> None:
        """The FreeBSD virtual machine's log is the only account of this failure."""
        control = CONTROL_HEADER + "Build-Depends: cmake,\n               libinvented-dev,\n"
        with self.assertRaises(ValueError) as failure:
            TRANSLATE(control)
        self.assertIn("libinvented-dev", str(failure.exception))
        self.assertIn("tools/freebsd-packages.py", str(failure.exception))

    def test_missing_build_depends_is_rejected(self) -> None:
        """An unparsable control file must not translate to an empty install."""
        with self.assertRaises(ValueError):
            TRANSLATE(CONTROL_HEADER)

    def test_version_and_profile_qualifiers_are_ignored(self) -> None:
        """debian/control qualifies names with versions and build profiles."""
        control = (
            CONTROL_HEADER
            + "Build-Depends: cmake (>= 3.24),\n               dbus-daemon <!nocheck>,\n"
        )
        self.assertEqual(TRANSLATE(control), ["cmake", "dbus"])
