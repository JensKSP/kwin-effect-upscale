#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject agent-specific tooling and attribution anywhere in the tree.

This enforces the "No AI artifacts in this repository" rule of AGENTS.md. The
repository's AGENTS.md files and temporary slice documents under doc/agents/
are permitted by that rule.

Run without arguments it checks what is staged, which is how the pre-commit
hook calls it. With --all it checks every tracked file, which is what the
pre-push hook and CI do, because a commit hook can be skipped with
git commit --no-verify.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from ai_attribution import PATTERN_FILES, is_attributed, is_forbidden_path


def tracked_files(*, everything: bool) -> list[str]:
    """List the repository-relative paths to check."""
    command = (
        ("git", "ls-files")
        if everything
        else ("git", "diff", "--cached", "--name-only", "--diff-filter=ACMR")
    )
    completed = subprocess.run(command, capture_output=True, text=True, check=True)
    return completed.stdout.splitlines()


def readable_text(path: Path) -> str | None:
    """Return the text of *path*, or None when it is binary or unreadable.

    A NUL byte is what git and grep both take as the mark of a binary file.
    Undecodable bytes are replaced rather than raising, because the patterns
    are ASCII and one bad byte must not let the rest of a file go unread.
    """
    try:
        content = path.read_bytes()
    except OSError:
        return None
    if b"\0" in content:
        return None
    return content.decode("utf-8", errors="replace")


def main() -> int:
    """Check the selected files and return 1 when one of them breaks the rule."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--all",
        action="store_true",
        help="check every tracked file rather than what is staged",
    )
    arguments = parser.parse_args()

    status = 0
    for name in tracked_files(everything=arguments.all):
        if is_forbidden_path(name):
            print(
                f"error: {name} is agent leftover; see the permitted files in AGENTS.md",
                file=sys.stderr,
            )
            status = 1

        if name in PATTERN_FILES:
            continue
        path = Path(name)
        if not path.is_file():
            continue
        text = readable_text(path)
        if text is not None and is_attributed(text):
            print(
                f"error: {name} mentions how it was written; say what the code does instead",
                file=sys.stderr,
            )
            status = 1

    if status != 0:
        print('See AGENTS.md, section "No AI artifacts in this repository".', file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
