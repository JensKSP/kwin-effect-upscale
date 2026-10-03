# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for reading versions and languages in the package session check."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from package_session import (
    ALWAYS,
    CATALOGUES,
    pictured,
    speaks,
    translation,
    upstream,
    versions_in,
)


class PackageSessionTest(unittest.TestCase):
    """Read versions out of libraries and languages out of the effect's status."""

    def test_reads_versions_kept_as_qt_literals(self) -> None:
        """Find a version Qt keeps as a UTF-16 literal."""
        binary = b"\x7fELF\x00" + "0.4.0+git20261002.3c574c62e2".encode("utf-16-le") + b"\x00\x00"
        self.assertIn("0.4.0+git20261002.3c574c62e2", versions_in(binary))

    def test_reads_versions_at_an_odd_offset(self) -> None:
        """Find a literal that starts at an odd byte."""
        binary = b"\x01" + "0.3.0~trixie".encode("utf-16-le")
        self.assertIn("0.3.0~trixie", versions_in(binary))

    def test_ignores_bytes_that_only_look_like_digits(self) -> None:
        """Find nothing in eight-bit text."""
        self.assertEqual(versions_in(b"0.4.0"), [])

    def test_names_the_version_as_the_build_does(self) -> None:
        """Keep a distribution's suffix and drop only a packaging revision."""
        # As the Debian 13 session named them on 2026-10-03.
        nightly = "0.4.0+git20261002.3c574c62e2~trixie"
        self.assertEqual(upstream(nightly), nightly)
        self.assertEqual(upstream("0.3.0~trixie"), "0.3.0~trixie")
        self.assertEqual(upstream("0.4.0-1"), "0.4.0")
        self.assertEqual(upstream("0.4.0"), "0.4.0")

    def test_every_shipped_language_translates_the_standing_line(self) -> None:
        """Find the standing line translated in each shipped catalogue."""
        for language in ("de", "fr", "es"):
            catalogue = (CATALOGUES / language / "kwin_effect_upscale.po").read_text(
                encoding="utf-8"
            )
            found = translation(catalogue, ALWAYS)
            self.assertTrue(found, language)
            self.assertNotEqual(found, ALWAYS, language)

    def test_recognises_the_language_of_a_status(self) -> None:
        """Tell a German status from a French one."""
        status = "upscale:\nstatus: Inaktiv\nHDR folgt der Farbverwaltung von KWin.\n"
        self.assertTrue(speaks(status, "de"))
        self.assertFalse(speaks(status, "fr"))

    def test_an_english_status_speaks_no_other_language(self) -> None:
        """Find no translation in an English status."""
        self.assertFalse(speaks(f"status: Inactive\n{ALWAYS}\n", "de"))

    def test_both_pictures_are_needed(self) -> None:
        """Count a look as pictured only with both files, never with an error instead."""
        self.assertTrue(pictured({"settings": "de-settings.png", "game picture": "de-game.png"}))
        failed = {"settings": "de-settings.png", "game picture": "No screenshot was taken"}
        self.assertFalse(pictured(failed))
        self.assertFalse(pictured({"settings": "", "game picture": ""}))


if __name__ == "__main__":
    unittest.main()
