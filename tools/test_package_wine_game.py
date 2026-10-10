# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the check of SuperTuxKart for Windows under Wine."""

import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import package_wine as pw
import package_wine_game as pg


class WineGameTest(unittest.TestCase):
    """Take only the published release, its 64-bit game, and a player's settings."""

    def test_refuses_a_download_that_is_not_the_release(self) -> None:
        """A file whose digest is not the published one is never unpacked or run."""
        with (
            tempfile.TemporaryDirectory() as directory,
            mock.patch("urllib.request.urlopen", return_value=io.BytesIO(b"not the game")),
            self.assertRaises(RuntimeError),
        ):
            pg.fetch(Path(directory))

    def test_starts_the_x86_64_game(self) -> None:
        """Of SuperTuxKart 1.5's four builds, the one Wine here runs is started."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for build in ("build-aarch64", "build-armv7", "build-i686", "build-x86_64"):
                (root / "stk-code" / build / "bin").mkdir(parents=True)
                (root / "stk-code" / build / "bin" / "supertuxkart.exe").write_text("")
            self.assertEqual(pg.executable(root).parent.parent.name, "build-x86_64")

    def test_refuses_a_release_without_an_x86_64_build(self) -> None:
        """Another architecture's game is never started in its place."""
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(RuntimeError):
            root = Path(directory)
            (root / "build-aarch64").mkdir()
            (root / "build-aarch64" / "supertuxkart.exe").write_text("")
            pg.executable(root)

    def test_names_no_game_where_there_is_none(self) -> None:
        """An unpacked release without the executable fails rather than runs something else."""
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(RuntimeError):
            pg.executable(Path(directory))

    def test_keeps_the_settings_where_windows_does(self) -> None:
        """SuperTuxKart for Windows reads its settings from %APPDATA% in the prefix."""
        self.assertIn("AppData/Roaming/supertuxkart", str(pg.SETTINGS))
        self.assertTrue(str(pg.SETTINGS).startswith(str(pw.PREFIX)))


if __name__ == "__main__":
    unittest.main()
