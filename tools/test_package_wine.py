# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the check of a Windows game on Wine's Wayland driver."""

import sys
import types
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import package_wine as pw

RED, BLUE = (230, 10, 20), (10, 20, 230)


def picture_module(width: int, split: int) -> types.ModuleType:
    """Make a stand-in for Pillow whose picture is red left of a column and blue from it."""
    image = mock.MagicMock()
    image.__enter__.return_value = image
    image.convert.return_value = image
    image.size = (width, 2160)
    image.getpixel.side_effect = lambda point: RED if point[0] < split else BLUE
    module = types.ModuleType("PIL")
    module.Image = mock.Mock(open=mock.Mock(return_value=image))  # type: ignore[attr-defined]
    return module


class WineTest(unittest.TestCase):
    """Run the probe on Wayland alone, and read where its picture splits."""

    def test_leaves_the_x11_display_out(self) -> None:
        """Give Wine no display to fall back to, and the prefix and quiet it needs."""
        environment = pw.wayland_only({"DISPLAY": ":0", "WAYLAND_DISPLAY": "wayland-0"})
        self.assertNotIn("DISPLAY", environment)
        self.assertEqual(environment["WAYLAND_DISPLAY"], "wayland-0")
        self.assertEqual(environment["WINEPREFIX"], str(pw.PREFIX))

    def test_finds_the_picture_split_at_the_middle(self) -> None:
        """A probe enlarged whole splits red from blue at the middle of the output."""
        with mock.patch.dict(sys.modules, {"PIL": picture_module(3840, 1920)}):
            self.assertIn("split at the middle of 3840", pw.split_at_middle(Path("p.png")))

    def test_refuses_a_picture_drawn_at_its_own_size(self) -> None:
        """A 2560-wide picture shown unenlarged splits at 1280, not at the middle."""
        with mock.patch.dict(sys.modules, {"PIL": picture_module(3840, 1280)}):
            self.assertEqual(pw.split_at_middle(Path("p.png")), "")


if __name__ == "__main__":
    unittest.main()
