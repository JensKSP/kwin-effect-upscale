# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require successful supported-platform jobs before a candidate can publish."""

import json
import os


def passed(results: dict[str, dict[str, object]]) -> bool:
    """Accept only successes and skips explicitly selected by the scope job."""
    if any(results.get(name, {}).get("result") != "success" for name in ("commits", "checks")):
        return False
    outputs = results["commits"].get("outputs")
    if not isinstance(outputs, dict):
        return False
    # arm64 follows the build flag: a documentation-only change has nothing to
    # compile on either architecture, and anything else has to compile on both.
    for flag, job in (
        ("build", "instrumentation"),
        ("build", "arm64"),
        ("packaging", "package"),
        ("packaging", "package-test"),
    ):
        if outputs.get(flag) not in ("true", "false"):
            return False
        expected = "success" if outputs[flag] == "true" else "skipped"
        if results.get(job, {}).get("result") != expected:
            return False
    return True


if __name__ == "__main__":
    jobs = json.loads(os.environ["RESULTS"])
    if not passed(jobs):
        message = f"Required quality checks did not pass: {jobs}"
        raise SystemExit(message)
