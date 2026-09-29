#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Say whether a commit's own CI run finished, and how, for the nightly.

    ci-conclusion.py <owner/repository> <commit>

Prints `state=passed`, `state=missing` or `state=failed` for GITHUB_OUTPUT. The
nightly builds a commit whose push, or pull request, already ran CI; running it
again cost a third of the nightly (item 81 of the open list, decided by Jens on
2026-09-29). A run that finished counts; one still going or cancelled does not,
so the nightly then runs CI itself rather than trust something unfinished. The
token is read from GH_TOKEN, as the gh command line reads it.
"""

from __future__ import annotations

import json
import subprocess
import sys

# A run with one of these conclusions said nothing about the commit.
UNDECIDED = {"cancelled", "skipped", "stale", "neutral", ""}


def state(runs: list[dict[str, str]]) -> str:
    """Say what the newest finished CI run of a commit concluded."""
    finished = [
        run
        for run in runs
        if run.get("status") == "completed" and run.get("conclusion", "") not in UNDECIDED
    ]
    if not finished:
        return "missing"
    newest = max(finished, key=lambda run: run.get("created_at", ""))
    return "passed" if newest["conclusion"] == "success" else "failed"


def runs_of(repository: str, commit: str) -> list[dict[str, str]]:
    """Ask GitHub for the CI runs of a commit: a handful, well within one page."""
    answer = subprocess.run(
        [
            "gh",
            "api",
            f"repos/{repository}/actions/workflows/ci.yml/runs?head_sha={commit}&per_page=100",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if answer.returncode:
        message = f"GitHub did not list the runs: {answer.stderr.strip()}"
        raise RuntimeError(message)
    runs: list[dict[str, str]] = json.loads(answer.stdout)["workflow_runs"]
    return runs


def main(argv: list[str]) -> int:
    """Print the commit's CI state, and fail where its CI failed."""
    if len(argv) != 2:  # noqa: PLR2004
        print(__doc__, file=sys.stderr)
        return 2
    found = state(runs_of(argv[0], argv[1]))
    print(f"state={found}")
    return 1 if found == "failed" else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
