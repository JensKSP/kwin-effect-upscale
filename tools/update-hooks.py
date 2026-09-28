#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Move the pinned pre-commit hooks to their newest versions, holding the held ones.

    update-hooks.py [--config .pre-commit-config.yaml]

`pre-commit autoupdate` moves every remote repository to the newest tag on its
default branch. That can be older than the pin, when a project tags a release
off that branch, and a pin never moves back: it stays where it is. A
repository whose `rev` line ends in `# held to <prefix>` is left out of that
and moves only to the newest tag that starts with the prefix: clang-format is
held to the major version Debian Trixie ships, because another one formats
differently. Local hooks, and the versions in `additional_dependencies`, are
not this tool's.

The configuration is rewritten in place, and what moved is printed as a
Markdown table for the run's summary. Nothing is committed or pushed.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPOSITORY = re.compile(r"^\s*-\s*repo:\s*(\S+)\s*$")
REVISION = re.compile(r"^(\s*rev:\s*)(\S+)(\s*#\s*held to\s+(\S+))?\s*$")


def pins(text: str) -> dict[str, tuple[str, str]]:
    """Map each remote repository to its revision and the prefix it is held to."""
    found: dict[str, tuple[str, str]] = {}
    repository = ""
    for line in text.splitlines():
        if match := REPOSITORY.match(line):
            repository = match[1]
        elif (match := REVISION.match(line)) and repository not in ("", "local", "meta"):
            found[repository] = (match[2], match[4] or "")
            repository = ""
    return found


def version(tag: str) -> tuple[int, ...]:
    """Order tags by the numbers in them, so v19.1.10 follows v19.1.9."""
    return tuple(int(number) for number in re.findall(r"\d+", tag))


def newest(repository: str, prefix: str) -> str:
    """Name the newest release tag of a repository that starts with the prefix."""
    listing = subprocess.check_output(
        ["git", "ls-remote", "--tags", "--refs", repository], text=True
    )
    tags = [line.split("refs/tags/", 1)[1] for line in listing.splitlines() if "refs/tags/" in line]
    # A release is a tag of numbers alone after the prefix: a candidate such as
    # v19.1.0-rc1 or a tag of some other scheme is no step to take.
    releases = [tag for tag in tags if re.fullmatch(re.escape(prefix) + r"[\d.]*", tag)]
    if not releases:
        message = f"{repository} has no tag starting with {prefix}"
        raise ValueError(message)
    return max(releases, key=version)


def pin(text: str, repository: str, revision: str) -> str:
    """Set one repository's revision, keeping its line otherwise as it is."""
    lines = text.splitlines(keepends=True)
    current = ""
    for index, line in enumerate(lines):
        if match := REPOSITORY.match(line):
            current = match[1]
        elif current == repository and (match := REVISION.match(line)):
            lines[index] = line.replace(match[2], revision, 1)
            break
    return "".join(lines)


def update(config: Path) -> list[tuple[str, str, str]]:
    """Update the configuration in place and return what moved, from and to."""
    before = pins(config.read_text())
    free = [repository for repository, (_, prefix) in before.items() if not prefix]
    if free:
        arguments = [argument for repository in free for argument in ("--repo", repository)]
        subprocess.run(
            ["pre-commit", "autoupdate", "--config", str(config), *arguments],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    text = config.read_text()
    updated = pins(text)
    for repository, (revision, prefix) in before.items():
        target = newest(repository, prefix) if prefix else updated[repository][0]
        if version(target) < version(revision):
            target = revision
        if target != updated[repository][0]:
            text = pin(text, repository, target)
    config.write_text(text)
    after = pins(text)
    return [
        (repository, revision, after[repository][0])
        for repository, (revision, _) in before.items()
        if after[repository][0] != revision
    ]


def report(moved: list[tuple[str, str, str]]) -> str:
    """Say what moved, as Markdown."""
    if not moved:
        return "Every pinned hook is at its newest version.\n"
    rows = ["| Repository | From | To |", "| --- | --- | --- |"]
    rows += [f"| {repository} | {old} | {new} |" for repository, old, new in moved]
    return "\n".join(rows) + "\n"


def main(argv: list[str] | None = None) -> int:
    """Update the configuration named on the command line."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(".pre-commit-config.yaml"))
    arguments = parser.parse_args(argv)
    sys.stdout.write(report(update(arguments.config)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
