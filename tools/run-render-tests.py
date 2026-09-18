# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the configured rendering tests in the project's build container."""

import os
import subprocess

if __name__ == "__main__":
    raise SystemExit(
        subprocess.call(
            [
                "ctest",
                "--test-dir",
                os.environ.get("UPSCALE_BUILD_DIR", "build"),
                "--output-on-failure",
                "--no-tests=error",
                "-R",
                "^upscale-(render|config)",
            ]
        )
    )
