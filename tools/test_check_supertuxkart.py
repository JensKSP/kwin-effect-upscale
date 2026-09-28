# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check what makes a SuperTuxKart cell pass, without running the game."""

import copy
import runpy
import unittest
import xml.etree.ElementTree as ET  # nosec B405 - Only a string this test made is parsed.
from pathlib import Path
from typing import Any, cast

SCRIPT = runpy.run_path(str(Path(__file__).with_name("check-supertuxkart.py")))
judge = SCRIPT["judge"]
game_configuration = SCRIPT["game_configuration"]
CELLS = SCRIPT["CELLS"]

PASSING = {
    "enlarged": True,
    "during": {
        "supplied": "2560x1440",
        "destination": "3840x2160",
        "scaling": "1",
        "window": "supertuxkart-supertuxkart",
    },
    "sizes": {"screen": "3840x2160", "window": "3840x2160", "plain": "3840x2160"},
    "sharpness": {"screen": 1.28, "window": 0.99, "after": 0.99, "plain": 0.99},
    "likeness": {"window": 0.04, "plain": 0.04},
}


class JudgeTest(unittest.TestCase):
    """Each way a cell can fall short is named."""

    def test_a_cell_that_passes(self) -> None:
        """The game enlarged from 2560 x 1440 to the screen, and the screen shows it."""
        self.assertEqual(judge(PASSING), [])

    def test_not_run(self) -> None:
        """A session that left no result did not run, which is not a pass."""
        self.assertEqual(judge({}), [SCRIPT["NOT_RUN"]])

    def test_never_enlarged(self) -> None:
        """A game the effect never enlarged fails, with what the effect said."""
        problems = judge({"enlarged": False, "waited": {"selected": "0"}})
        self.assertEqual(len(problems), 1)
        self.assertIn("never enlarged", problems[0])

    def test_a_picture_that_failed(self) -> None:
        """A picture KWin did not deliver fails the cell with the reason."""
        result = copy.deepcopy(PASSING) | {"failure": "DBusException: NoReply"}
        self.assertEqual(judge(result), ["a picture could not be taken: DBusException: NoReply"])

    def test_each_shortfall(self) -> None:
        """A full-size buffer, a partial draw, a stop, a wrong picture, no added detail."""
        for path, value, said in (
            (("during", "supplied"), "3840x2160", "supplied 3840x2160"),
            (("during", "destination"), "1920x1080", "not the whole screen"),
            (("during", "scaling"), "0", "stopped enlarging"),
            (("sizes", "screen"), "1920x1080", "screen captured"),
            (("likeness", "plain"), 40.0, "does not show"),
            (("sharpness", "screen"), 1.0, "no more detail"),
        ):
            with self.subTest(field=path):
                result = copy.deepcopy(PASSING)
                cast("dict[str, Any]", result[path[0]])[path[1]] = value
                problems = judge(result)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn(said, problems[0])


class GameConfigurationTest(unittest.TestCase):
    """The game's own settings name the renderer and the kind of fullscreen."""

    def test_every_cell(self) -> None:
        """Six cells: both protocols, OpenGL, and Vulkan borderless and exclusive."""
        self.assertEqual(len({cell.name for cell in CELLS}), 6)
        for cell in CELLS:
            with self.subTest(cell=cell.name):
                video = ET.fromstring(game_configuration(cell)).find("Video")  # noqa: S314  # nosec B314
                if video is None:
                    self.fail("no Video element")
                self.assertEqual(video.get("fullscreen"), "true")
                self.assertEqual(video.get("real_width"), "3840")
                self.assertEqual(video.get("render_driver"), cell.renderer)
                self.assertEqual(
                    video.get("vulkan_fullscreen_desktop"), "true" if cell.desktop else "false"
                )


if __name__ == "__main__":
    unittest.main()
