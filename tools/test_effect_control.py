# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a measurement run sets the resolution the effect actually reads."""

import re
import subprocess
import unittest
from pathlib import Path
from unittest import mock

from effect_control import PRESETS, configure

RESOLUTION_HEADER = Path(__file__).parent.parent / "src/plugins/upscale/resolution.h"


def enumerated_presets() -> list[str]:
    """Return the ResolutionPreset members in order, spelled as the tool spells them."""
    source = RESOLUTION_HEADER.read_text()
    body = re.search(r"enum class ResolutionPreset \{(.*?)\};", source, re.DOTALL)
    if body is None:
        message = f"no ResolutionPreset in {RESOLUTION_HEADER}"
        raise AssertionError(message)
    members = [name.strip() for name in body.group(1).split(",") if name.strip()]
    return [re.sub(r"(?<!^)(?=[A-Z])", "-", name).lower() for name in members]


class PresetValueTest(unittest.TestCase):
    """The numbers written are the effect's own, not a copy that can drift."""

    def test_values_follow_the_enum(self) -> None:
        """Dropping a member from the enum shifts every value after it.

        That is how the tool came to write Balanced's number for Quality: the
        enum lost Automatic and the table here kept counting from it.
        """
        names = enumerated_presets()
        self.assertEqual(list(PRESETS), names)
        self.assertEqual(list(PRESETS.values()), list(range(len(names))))


class ConfigureTest(unittest.TestCase):
    """What a run writes, and what it accepts as written."""

    def setUp(self) -> None:
        """Record every helper call and answer it without a session."""
        self.calls: list[list[str]] = []
        self.stored = ""
        # What kreadconfig6 answers instead of the value written, if anything.
        self.read_back: str | None = None
        patcher = mock.patch("effect_control.run_command", side_effect=self.answer)
        patcher.start()
        self.addCleanup(patcher.stop)
        tool = mock.patch("effect_control.qdbus", return_value="qdbus6")
        tool.start()
        self.addCleanup(tool.stop)

    def answer(self, arguments: list[str]) -> subprocess.CompletedProcess[str]:
        """Stand in for kwriteconfig6, kreadconfig6 and qdbus."""
        self.calls.append(arguments)
        if arguments[0] == "kwriteconfig6" and arguments[-2] == "Resolution":
            self.stored = arguments[-1]
        output = ""
        if arguments[0] == "kreadconfig6":
            output = self.stored if self.read_back is None else self.read_back
        return subprocess.CompletedProcess(arguments, 0, output, "")

    def test_writes_the_resolution_key(self) -> None:
        """The legacy Preset key is ignored once a Resolution key exists."""
        self.assertEqual(configure("quality", sharpening=False), "")
        written = {call[-2]: call[-1] for call in self.calls if call[0] == "kwriteconfig6"}
        self.assertEqual(written, {"Resolution": "2", "Sharpening": "false"})
        read = [call for call in self.calls if call[0] == "kreadconfig6"]
        self.assertEqual(read[0][-1], "Resolution")

    def test_a_value_that_did_not_stick_is_reported(self) -> None:
        """A run measuring the previous resolution must not produce a number."""
        self.read_back = "4\n"
        self.assertEqual(configure("quality", sharpening=True), "the resolution is 4, not quality")


if __name__ == "__main__":
    unittest.main()
