# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for reading versions and languages in the package session check."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from package_session import ALWAYS, CATALOGUES, speaks, translation, versions_in  # noqa: E402


class PackageSessionTest(unittest.TestCase):
    def test_reads_versions_kept_as_qt_literals(self) -> None:
        binary = b"\x7fELF\x00" + "0.4.0+git20261002.3c574c62e2".encode("utf-16-le") + b"\x00\x00"
        self.assertIn("0.4.0+git20261002.3c574c62e2", versions_in(binary))

    def test_reads_versions_at_an_odd_offset(self) -> None:
        binary = b"\x01" + "0.3.0~trixie".encode("utf-16-le")
        self.assertIn("0.3.0~trixie", versions_in(binary))

    def test_ignores_bytes_that_only_look_like_digits(self) -> None:
        self.assertEqual(versions_in(b"0.4.0"), [])

    def test_every_shipped_language_translates_the_standing_line(self) -> None:
        for language in ("de", "fr", "es"):
            catalogue = (CATALOGUES / language / "kwin_effect_upscale.po").read_text(encoding="utf-8")
            found = translation(catalogue, ALWAYS)
            self.assertTrue(found, language)
            self.assertNotEqual(found, ALWAYS, language)

    def test_recognises_the_language_of_a_status(self) -> None:
        status = "upscale:\nstatus: Inaktiv\nHDR folgt der Farbverwaltung von KWin.\n"
        self.assertTrue(speaks(status, "de"))
        self.assertFalse(speaks(status, "fr"))

    def test_an_english_status_speaks_no_other_language(self) -> None:
        self.assertFalse(speaks(f"status: Inactive\n{ALWAYS}\n", "de"))


if __name__ == "__main__":
    unittest.main()
