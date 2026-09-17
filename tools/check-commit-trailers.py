#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject a commit message that names a tool as author or helper.

This enforces the second rule of AGENTS.md and KDE's contribution rules.

    check-commit-trailers.py [<range>]

Without a range it checks the single commit at HEAD. CI passes the range of the
branch under review. A pre-commit hook cannot do this check at all, because it
never sees the message, so CI is the place that actually guarantees it.
"""

from __future__ import annotations

import argparse
import subprocess
import sys

from ai_attribution import is_attributed

GUIDELINES = "https://community.kde.org/Guidelines_and_HOWTOs/Maintainers_and_Contributions"


def git(*arguments: str) -> str:
    """Return the output of a git command, raising when it fails."""
    completed = subprocess.run(
        ("git", *arguments),
        capture_output=True,
        text=True,
        check=True,
    )
    return completed.stdout


def commits_in(revisions: str) -> list[str]:
    """List the commits *revisions* names, falling back to the whole history.

    A range whose endpoints git cannot resolve is not an error worth failing
    on: CI hands us one on the first push of a branch, when the base commit is
    not there yet. Checking every commit reachable from HEAD is the safe answer.
    """
    try:
        git("rev-parse", revisions)
    except subprocess.CalledProcessError:
        revisions = "HEAD"

    try:
        listed = git("rev-list", revisions)
    except subprocess.CalledProcessError:
        listed = git("rev-list", "-1", "HEAD")

    return listed.split()


def main() -> int:
    """Check every commit in the given range and return 1 when one is bad."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "revisions",
        nargs="?",
        default="HEAD~1..HEAD",
        help="a git revision range; the commit at HEAD by default",
    )
    arguments = parser.parse_args()

    status = 0
    for commit in commits_in(arguments.revisions):
        if is_attributed(git("log", "-1", "--format=%B", commit)):
            subject = git("log", "-1", "--format=%h %s", commit).strip()
            print(f"error: {subject} carries an AI attribution trailer", file=sys.stderr)
            status = 1

    if status != 0:
        print("KDE does not accept these trailers; see AGENTS.md and", file=sys.stderr)
        print(GUIDELINES, file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
