# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Require successful supported-platform jobs before a candidate can publish."""

import json
import os


def passed(results: dict[str, dict[str, object]]) -> bool:
    """Skipped required jobs fail; the conditional packaging smoke job may skip."""
    required = ("commits", "checks", "instrumentation")
    return all(results.get(name, {}).get("result") == "success" for name in required) and (
        results.get("package-smoke", {}).get("result") in ("success", "skipped")
    )


if __name__ == "__main__":
    jobs = json.loads(os.environ["RESULTS"])
    if not passed(jobs):
        message = f"Required quality checks did not pass: {jobs}"
        raise SystemExit(message)
