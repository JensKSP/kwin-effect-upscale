#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Commit files from a working copy onto a branch, leaving the checkout alone.

    commit-to-branch.py BRANCH MESSAGE_FILE --from DIRECTORY PATH...

Several sessions share one checkout here, each on its own branch, so work for
another branch is done in a copy under build/ and committed from there. This
writes each PATH as DIRECTORY has it - added, changed, or removed where it is
missing - onto BRANCH's tip through a temporary index, and moves BRANCH only
if nobody moved it meanwhile. The checkout's index, its files and the branch it
has checked out are not touched, and the commit is made with the identity the
repository is configured with, like any other.

A copy under build/ is never where work stays: commit each step from it as
soon as it is done, since build/ holds only what can be thrown away.

No hook runs on such a commit. Run both pre-commit stages in the copy first,
in the maintained container, as doc/checks.md asks of every commit.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from git_fixture import without_repository


def git(repository: Path, *arguments: str, environment: dict[str, str] | None = None) -> str:
    """Run git in the repository and answer what it printed, or fail with what it said.

    A repository location the process inherited, as from a hook, would win
    over -C, and the commit would land in that repository instead.
    """
    done = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        capture_output=True,
        text=True,
        env=environment or without_repository(dict(os.environ)),
        check=False,
    )
    if done.returncode:
        message = f"git {arguments[0]} failed: {done.stderr.strip() or done.stdout.strip()}"
        raise RuntimeError(message)
    return done.stdout.strip()


def commit(repository: Path, branch: str, message: Path, source: Path, paths: list[str]) -> str:
    """Commit the paths as the source has them onto the branch, and answer the commit."""
    reference = f"refs/heads/{branch}"
    start = git(repository, "rev-parse", "--verify", reference)
    with tempfile.TemporaryDirectory(prefix="commit-to-branch-") as scratch:
        index = {"GIT_INDEX_FILE": str(Path(scratch) / "index")}
        environment = without_repository(dict(os.environ)) | index
        git(repository, "read-tree", start, environment=environment)
        for path in paths:
            file = source / path
            if file.exists():
                mode = "100755" if os.access(file, os.X_OK) else "100644"
                blob = git(repository, "hash-object", "-w", "--", str(file))
                git(
                    repository,
                    *("update-index", "--add", "--cacheinfo", f"{mode},{blob},{path}"),
                    environment=environment,
                )
            else:
                git(
                    repository,
                    "update-index",
                    "--force-remove",
                    "--",
                    path,
                    environment=environment,
                )
        tree = git(repository, "write-tree", environment=environment)
    if tree == git(repository, "rev-parse", f"{start}^{{tree}}"):
        message_text = "nothing to commit: the paths are as the branch has them"
        raise RuntimeError(message_text)
    created = git(repository, "commit-tree", tree, "-p", start, "-F", str(message))
    subject = message.read_text().splitlines()[0]
    git(repository, "update-ref", "-m", f"commit: {subject}", reference, created, start)
    return created


def main(argv: list[str] | None = None) -> int:
    """Commit, and print the new commit."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("branch")
    parser.add_argument("message", type=Path, help="file holding the commit message")
    parser.add_argument("paths", nargs="+", help="paths relative to the repository's root")
    parser.add_argument("--from", dest="source", type=Path, required=True, help="the working copy")
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parent.parent)
    options = parser.parse_args(argv)
    created = commit(
        options.repository, options.branch, options.message, options.source.resolve(), options.paths
    )
    print(git(options.repository, "log", "--oneline", "-1", created))
    return 0


if __name__ == "__main__":
    sys.exit(main())
