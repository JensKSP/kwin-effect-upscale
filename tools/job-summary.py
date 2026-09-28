#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write one check job's summary for the run page, in Markdown.

    job-summary.py --mode <mode> --outcome <outcome> --environment <name>
                   --architecture <arch> [--build-only] [--build <directory>]

The outcome is the check step's own, as GitHub reports it, so a check that was
skipped, cancelled or never ran is said to be so and never shown as passed. The
figures come from the reports the check left: ctest's JUnit report for the
tests, gcovr's summary for the coverage. A report that is missing is said to be
missing rather than read as nothing having failed.
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET  # nosec B405 - Only the report our own ctest wrote is parsed.
from pathlib import Path

OUTCOMES = {
    "success": "passed",
    "failure": "**failed**",
    "cancelled": "cancelled, not run to the end",
    "skipped": "skipped, not run",
}


def tests(report: Path) -> str:
    """Say how the tests went, from ctest's JUnit report."""
    if not report.is_file():
        return "no test report"
    try:
        root = ET.parse(report).getroot()  # noqa: S314  # nosec B314
    except ET.ParseError:
        return "test report unreadable"
    suite = root if root.tag == "testsuite" else root.find("testsuite")
    if suite is None:
        return "test report unreadable"
    counts = {
        name: int(suite.get(name, "0"))
        for name in ("tests", "failures", "errors", "skipped", "disabled")
    }
    failed = counts["failures"] + counts["errors"]
    text = f"{counts['tests']} run, {failed} failed"
    if counts["skipped"] or counts["disabled"]:
        text += f", {counts['skipped'] + counts['disabled']} skipped"
    return text


def coverage(summary: Path) -> str:
    """Say what the line coverage came to, from gcovr's summary."""
    if not summary.is_file():
        return "no coverage report"
    try:
        percent = json.loads(summary.read_text())["line_percent"]
    except (ValueError, KeyError):
        return "coverage report unreadable"
    return f"{percent:.1f} % of lines"


def summary(  # noqa: PLR0913 - One argument per fact the job knows about itself.
    mode: str,
    outcome: str,
    environment: str,
    architecture: str,
    build: Path,
    *,
    build_only: bool = False,
) -> str:
    """Compose the job's Markdown summary."""
    result = OUTCOMES.get(outcome, "unknown (" + (outcome or "no outcome") + ")")
    lines = [f"### {environment} {architecture}: {mode}", "", f"- Checks: {result}"]
    if build_only:
        lines.append("- Tests: not run, this platform is only built")
    elif mode not in ("lint", "docs", "tidy", "codeql"):
        lines.append(f"- Tests: {tests(build / mode / 'runtime-tests.xml')}")
    if mode == "coverage":
        lines.append(f"- Coverage: {coverage(build / mode / 'coverage' / 'summary.json')}")
    lines.append(f"- Reports: the artifact `reports-{environment}-{architecture}-{mode}`")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    """Print the summary for the check step described on the command line."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--outcome", default="")
    parser.add_argument("--environment", required=True)
    parser.add_argument("--architecture", required=True)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--build", type=Path, default=Path("build"))
    arguments = parser.parse_args(argv)
    sys.stdout.write(
        summary(
            arguments.mode,
            arguments.outcome,
            arguments.environment,
            arguments.architecture,
            arguments.build,
            build_only=arguments.build_only,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
