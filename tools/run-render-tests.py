# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the configured rendering tests in the project's build container."""

import os
import subprocess

if __name__ == "__main__":
    environment = dict(os.environ)
    # CTest 3.29+ chooses its native parallel level for an empty value. Older
    # supported CMake versions safely run serially; explicit limits win.
    environment.setdefault("CTEST_PARALLEL_LEVEL", "")
    environment.setdefault("ASAN_OPTIONS", "halt_on_error=1:detect_leaks=1:fast_unwind_on_malloc=0")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    # Qt and Mesa use atomic synchronization that is invisible in distribution
    # binaries. Only instrumented code can provide reliable race diagnostics.
    environment.setdefault("TSAN_OPTIONS", "halt_on_error=1:ignore_noninstrumented_modules=1")
    raise SystemExit(
        subprocess.call(
            [
                "ctest",
                "--test-dir",
                os.environ.get("UPSCALE_BUILD_DIR", "build"),
                "--output-on-failure",
                "--no-tests=error",
                "--timeout",
                "120",
                "--output-junit",
                "runtime-tests.xml",
                "-R",
                "^upscale-(render|config|resolution|integration|snapshot)",
            ],
            env=environment,
        )
    )
