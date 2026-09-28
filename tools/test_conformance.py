# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""A partial XTS run or a missing execution configuration cannot pass the gate."""

import contextlib
import io
import os
import runpy
import subprocess
import tempfile
import unittest
from pathlib import Path

from x11_conformance import configure_xts

CHECK = runpy.run_path(str(Path(__file__).with_name("check-conformance.py")))


class ConformanceTest(unittest.TestCase):
    """Retain failures that previously looked like a successful comparison."""

    def test_missing_cases_fail_even_when_shared_cases_pass(self) -> None:
        """Stopping after a passing prefix is not equivalent to running the suite."""
        for baseline, scaling in (
            ({"first": "pass", "last": "pass"}, {"first": "pass"}),
            ({"first": "pass"}, {"first": "pass", "last": "pass"}),
        ):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(CHECK["report"]("xts", CHECK["compare"](baseline, scaling)), 1)

    def test_runner_completion_requires_exit_and_final_results(self) -> None:
        """Identical interrupted arms must not validate each other's partial results."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = root / "piglit"
            results.mkdir()
            self.assertFalse(CHECK["completed"](root))
            (root / "exit-code").write_text("0\n")
            self.assertFalse(CHECK["completed"](root))
            (results / "results.json").write_text('{"tests": {}}')
            self.assertTrue(CHECK["completed"](root))
            (root / "exit-code").write_text("124\n")
            self.assertFalse(CHECK["completed"](root))

    def test_configuration_failure_stops_before_running_cases(self) -> None:
        """Do not continue past a failed upstream configuration generator."""
        with tempfile.TemporaryDirectory(prefix="xts setup ") as directory:
            root = Path(directory)
            (root / "config").mkdir()
            binaries = root / "bin"
            binaries.mkdir()
            tree = root / "suite"
            (tree / "xts5").mkdir(parents=True)
            (tree / "xts5" / "tetexec.cfg.in").write_text("XT_FONTPATH_BAD=/absent\n")
            command = binaries / "perl"
            command.write_text("#!/bin/sh\nexit 7\n")
            command.chmod(0o700)
            environment = {
                **os.environ,
                "PATH": f"{binaries}:{os.defpath}",
                "TET_ROOT": str(tree),
                "TET_CONFIG": str(root / "config" / "tetexec.cfg"),
            }
            with self.assertRaises(subprocess.CalledProcessError) as failure:
                configure_xts(root, environment)
            self.assertEqual(failure.exception.returncode, 7)

    def test_protocol_arms_require_their_own_policy(self) -> None:
        """Outer KWin routing alone cannot validate a bypassed nested proxy."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "session.log").write_text("effects: upscale\nUpscale X11 backend started\n")
            verify = CHECK["arm_engaged"]
            arm = CHECK["Arm"]
            bare = arm("bare", root, upscaled=False)
            present = arm("present", root, upscaled=False)
            scaling = arm("scaling", root, upscaled=True)
            self.assertFalse(verify(present, root, "xts"))
            (root / "nested.log").write_text("")
            self.assertTrue(verify(bare, root, "xts"))
            self.assertFalse(verify(present, root, "xts"))
            (root / "nested.log").write_text("Upscale X11 backend started\n")
            self.assertTrue(verify(present, root, "xts"))
            self.assertFalse(verify(scaling, root, "xts"))
            (root / "nested.log").write_text(
                "Upscale X11 backend started\nconnection display advertisement\n"
            )
            self.assertTrue(verify(scaling, root, "xts"))
            self.assertFalse(verify(present, root, "xts"))
            (root / "session.log").write_text("Upscale X11 backend started\n")
            self.assertFalse(verify(scaling, root, "xts"))
