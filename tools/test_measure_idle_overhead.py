# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for reading and pairing the idle-overhead measurement."""

import runpy
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

# The script imports its sibling the way every tool here does, which works
# because Python puts a script's own directory on the path. Loading it by path
# does not, so the directory is named before it is loaded.
sys.path.insert(0, str(Path(__file__).parent))

TOOL = runpy.run_path(str(Path(__file__).with_name("measure-idle-overhead.py")))
Run = TOOL["Run"]
parse_fps = TOOL["parse_fps"]
frame_time_deltas = TOOL["frame_time_deltas"]
summarize = TOOL["summarize"]
run_glmark2 = TOOL["run_glmark2"]

# glmark2's lines as it prints them, with the noise around them.
OUTPUT = """=======================================================
    glmark2 2023.01
=======================================================
[texture] duration=8: FPS: 18316 FrameTime: 0.055 ms
[shading] duration=8: FPS: 21370 FrameTime: 0.047 ms
[build] duration=8: FPS: 26119 FrameTime: 0.038 ms
=======================================================
                                  glmark2 Score: 21935
"""


class IdleOverheadTest(unittest.TestCase):
    def test_reads_each_scene(self) -> None:
        self.assertEqual(parse_fps(OUTPUT), {"texture": 18316, "shading": 21370, "build": 26119})

    def test_a_failed_run_reads_nothing(self) -> None:
        self.assertEqual(parse_fps("Error: main: Could not initialize canvas"), {})

    def test_pairs_by_frame_time(self) -> None:
        # 20 000 against 16 000 frames a second is 50 against 62.5 microseconds.
        runs = [Run(0, "A0", {"texture": 20000}), Run(0, "A1", {"texture": 16000})]
        self.assertAlmostEqual(frame_time_deltas(runs)["texture"][0], 12.5)

    def test_pairs_in_either_order(self) -> None:
        runs = [
            Run(0, "A0", {"texture": 20000}),
            Run(0, "A1", {"texture": 20000}),
            Run(1, "A1", {"texture": 10000}),
            Run(1, "A0", {"texture": 20000}),
        ]
        self.assertEqual(frame_time_deltas(runs)["texture"], [0.0, 50.0])

    def test_a_pair_missing_a_run_is_left_out(self) -> None:
        runs = [Run(0, "A0", {"texture": 20000}), Run(1, "A1", {"texture": 10000})]
        self.assertEqual(frame_time_deltas(runs), {})

    def test_summary(self) -> None:
        runs = []
        for pair in range(3):
            runs.append(Run(pair, "A0", {"texture": 20000, "shading": 25000, "build": 40000}))
            runs.append(Run(pair, "A1", {"texture": 16000, "shading": 25000, "build": 40000}))
        summary = summarize(runs)
        self.assertEqual(summary["pairs"], 3)
        self.assertEqual(summary["scenes"]["texture"]["ratio"], 0.8)
        self.assertEqual(summary["scenes"]["texture"]["added_microseconds_median"], 12.5)
        self.assertEqual(summary["scenes"]["shading"]["ratio"], 1.0)
        # Nine deltas, six of them nothing: the median over all is nothing.
        self.assertEqual(summary["added_microseconds_median"], 0.0)
        self.assertEqual(summary["added_microseconds_range"], [0.0, 12.5])

    def test_a_run_without_every_scene_stops_with_what_glmark2_said(self) -> None:
        failed = subprocess.CompletedProcess([], 1, "", "Error: Failed to set up the window")
        with mock.patch.dict(run_glmark2.__globals__, {"run_command": lambda _: failed}):
            with self.assertRaisesRegex(RuntimeError, "Failed to set up the window"):
                run_glmark2(8)

    def test_a_complete_run_answers_its_rates(self) -> None:
        done = subprocess.CompletedProcess([], 0, OUTPUT, "")
        with mock.patch.dict(run_glmark2.__globals__, {"run_command": lambda _: done}):
            self.assertEqual(run_glmark2(8)["build"], 26119)

    def test_no_runs_summarize_to_nothing(self) -> None:
        summary = summarize([])
        self.assertEqual(summary["scenes"], {})
        self.assertIsNone(summary["added_microseconds_median"])


if __name__ == "__main__":
    unittest.main()
