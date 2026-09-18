# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile and exercise the production resolution policy without a compositor."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class UpscalePolicyTest(unittest.TestCase):
    """Check pixel rounding, eligibility boundaries and the real RCAS bypass."""

    def test_resolution_policy(self) -> None:
        """Run assertions against the same header used by the effect and KCM."""
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build, prefix="resolution-test-") as directory:
            binary = Path(directory) / "resolution-test"
            subprocess.run(
                [
                    os.environ.get("CXX", "c++"),
                    "-std=c++23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / 'src/plugins/upscale'}",
                    str(ROOT / "tools/upscale-resolution-test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)
