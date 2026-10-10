# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the check of Windows graphics APIs under Wine."""

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
    """Run the probe on each driver as it needs, and read where its picture splits."""

    def test_leaves_the_x11_display_out(self) -> None:
        """Give Wine no display to fall back to, and the prefix and quiet it needs."""
        environment = pw.wayland_only({"DISPLAY": ":0", "WAYLAND_DISPLAY": "wayland-0"})
        self.assertNotIn("DISPLAY", environment)
        self.assertEqual(environment["WAYLAND_DISPLAY"], "wayland-0")
        self.assertEqual(environment["WINEPREFIX"], str(pw.PREFIX))

    def test_gives_each_driver_its_display(self) -> None:
        """Wine's X11 driver needs the X11 display; its Wayland driver must not fall back to it."""
        session = {"DISPLAY": ":0", "WAYLAND_DISPLAY": "wayland-0"}
        self.assertNotIn("DISPLAY", pw.for_driver(session, "wayland"))
        self.assertEqual(pw.for_driver(session, "x11")["DISPLAY"], ":0")
        self.assertEqual(pw.for_driver(session, "x11")["WINEPREFIX"], str(pw.PREFIX))

    def test_builds_every_part_of_the_probe(self) -> None:
        """Each API's part is compiled in, so a case never meets a probe without its API."""
        names = {source.name for source in pw.SOURCES}
        self.assertTrue(
            {"main.cpp", "opengl.cpp", "d3d9.cpp", "d3d11.cpp", "d3d12.cpp", "vulkan.cpp"} <= names
        )

    def test_runs_every_driver_api_and_mode(self) -> None:
        """Twenty cases, and the check passes only where every one of them did."""
        ran: list[str] = []

        def case(
            _probe: Path, _environment: dict[str, str], name: str, _directory: Path
        ) -> dict[str, object]:
            ran.append(name)
            return {"passed": name != "x11 d3d12 exclusive"}

        session = mock.Mock(environment={"DISPLAY": ":0"})
        result: dict[str, object] = {}
        with (
            mock.patch.object(pw, "run_case", side_effect=case),
            mock.patch.object(pw, "set_driver"),
            mock.patch("package_session.picture", return_value="wine-desktop.png"),
        ):
            pw.probe_runs(Path("probe.exe"), session, result, Path())
        self.assertEqual(len(ran), 20)
        self.assertEqual(len(set(ran)), 20)
        self.assertIn("wayland vulkan borderless", ran)
        self.assertFalse(result["passed"])

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
