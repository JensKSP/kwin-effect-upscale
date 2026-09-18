# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Report all production C++ coverage, including unexecuted translation units."""

import json
import os
import subprocess
from pathlib import Path


def main() -> int:
    """Fail below 90 percent or if a production translation unit is missing."""
    root = Path(__file__).resolve().parent.parent
    build = Path(os.environ.get("UPSCALE_BUILD_DIR", "build")).resolve()
    report = build / "coverage"
    report.mkdir(exist_ok=True)
    result = subprocess.run(
        [
            "gcovr",
            "-j",
            "0",
            "--root",
            str(root),
            "--filter",
            "src/plugins/upscale/",
            "--merge-mode-functions",
            "merge-use-line-min",
            "--txt",
            str(report / "coverage.txt"),
            "--xml",
            str(report / "coverage.xml"),
            "--html-details",
            str(report / "index.html"),
            "--json-summary",
            str(report / "summary.json"),
            "--fail-under-line",
            "90",
            "--print-summary",
            "--txt-branches",
            str(build),
        ],
        check=False,
    )
    if result.returncode:
        return result.returncode
    summary = json.loads((report / "summary.json").read_text())
    measured = {item["filename"] for item in summary["files"]}
    expected = {
        str(path.relative_to(root)) for path in (root / "src/plugins/upscale").rglob("*.cpp")
    }
    missing = expected - measured
    if missing:
        print(f"Production sources missing from coverage: {sorted(missing)}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
