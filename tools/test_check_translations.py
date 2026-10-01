# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the translation catalogue check."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).with_name("check-translations.py")
SOURCE = """#include <KLocalizedString>
QString a(int n) { return i18n("Renders at %1", n); }
QString b() { return i18nc("A corner", "Top left"); }
"""
HEADER = """msgid ""
msgstr ""
"Language: de\\n"
"Content-Type: text/plain; charset=UTF-8\\n"
"Plural-Forms: nplurals=2; plural=(n != 1);\\n"

"""
RENDERS = """#, kde-format
msgid "Renders at %1"
msgstr "{}"

"""
CORNER = """msgctxt "A corner"
msgid "Top left"
msgstr "{}"

"""


@unittest.skipUnless(shutil.which("xgettext") and shutil.which("msgattrib"), "needs gettext")
class TranslationCheckTest(unittest.TestCase):
    """Run the checker against a tree of one source file and one catalogue."""

    def setUp(self) -> None:
        """Keep all fixtures outside the source tree."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / "src").mkdir()
        (self.root / "src" / "a.cpp").write_text(SOURCE)
        (self.root / "po" / "de").mkdir(parents=True)
        self.catalogue = self.root / "po" / "de" / "kwin_effect_upscale.po"

    def check(self, *options: str) -> subprocess.CompletedProcess[str]:
        """Run the same entry point as the hook, on the fixture tree."""
        # In German, where gettext reports its statistics in German, which
        # the checker has to read all the same.
        environment = {**os.environ, "LANG": "de_DE.UTF-8", "LANGUAGE": "de"}
        environment.pop("LC_ALL", None)
        return subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(self.root), *options],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_complete(self) -> None:
        """Every string translated passes."""
        self.catalogue.write_text(
            HEADER + RENDERS.format("Rendert mit %1") + CORNER.format("Oben links")
        )
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing(self) -> None:
        """A string without a translation fails and is named."""
        self.catalogue.write_text(HEADER + RENDERS.format("Rendert mit %1") + CORNER.format(""))
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("1 untranslated", result.stderr)
        self.assertIn("Top left", result.stderr)

    def test_obsolete(self) -> None:
        """A translation of a string the sources no longer have fails."""
        gone = 'msgid "Gone"\nmsgstr "Weg"\n\n'
        self.catalogue.write_text(
            HEADER + RENDERS.format("Rendert mit %1") + CORNER.format("Oben links") + gone
        )
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("no longer have", result.stderr)

    def test_placeholder(self) -> None:
        """A translation that invents a placeholder fails msgfmt's check."""
        self.catalogue.write_text(
            HEADER + RENDERS.format("Rendert mit %1 und %2") + CORNER.format("Oben links")
        )
        self.assertEqual(self.check().returncode, 1)

    def test_update(self) -> None:
        """A new source string arrives untranslated and is then named."""
        self.catalogue.write_text(
            HEADER + RENDERS.format("Rendert mit %1") + CORNER.format("Oben links")
        )
        (self.root / "src" / "b.cpp").write_text('QString c() { return i18n("Bottom right"); }\n')
        result = self.check("--update")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Bottom right", result.stderr)
        self.assertIn('msgid "Bottom right"', self.catalogue.read_text())
        self.assertIn("Oben links", self.catalogue.read_text())


if __name__ == "__main__":
    unittest.main()
