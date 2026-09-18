# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Review dependencies a change introduces, against an explicit severity policy."""

import argparse
import json
import os
import subprocess
from pathlib import Path
from typing import NotRequired, TypedDict, cast


class Advisory(TypedDict):
    """One published advisory affecting an introduced dependency version."""

    severity: str
    advisory_ghsa_id: str
    advisory_summary: str
    advisory_url: str


class Change(TypedDict):
    """One dependency GitHub's graph sees added or removed by this change."""

    change_type: str
    manifest: str
    ecosystem: str
    name: str
    version: str
    vulnerabilities: list[Advisory]
    license: NotRequired[str | None]
    scope: NotRequired[str]


# GitHub's order, lowest first. An advisory whose severity is absent or spelled
# differently is treated as the highest: an unreadable verdict is not a pass.
SEVERITIES = ("low", "moderate", "high", "critical")

# Everything from moderate upwards blocks. This project has a handful of pinned
# GitHub Actions and no application dependency tree, so a stricter threshold
# costs little and a "high only" policy would let a moderate token leak through.
THRESHOLD = "moderate"

# GHSA identifiers this repository knowingly accepts, each with the reason it
# does not apply here and who decided. An empty policy is the honest default:
# an exception is added when a real advisory has been assessed, never to make a
# red check green.
EXCEPTIONS: dict[str, str] = {}


def rank(severity: str) -> int:
    """Order a severity, placing anything unrecognised above `critical`."""
    normalised = severity.strip().lower()
    return SEVERITIES.index(normalised) if normalised in SEVERITIES else len(SEVERITIES)


def introduced(changes: list[Change]) -> list[Change]:
    """Review what the change adds; a removed dependency needs no advisory."""
    return [change for change in changes if change["change_type"] == "added"]


def blocking(
    changes: list[Change],
    threshold: str = THRESHOLD,
    exceptions: dict[str, str] | None = None,
) -> list[tuple[Change, Advisory]]:
    """Select the advisories that must block, after recorded exceptions."""
    accepted = EXCEPTIONS if exceptions is None else exceptions
    limit = rank(threshold)
    return [
        (change, advisory)
        for change in introduced(changes)
        for advisory in change["vulnerabilities"]
        if rank(advisory["severity"]) >= limit and advisory["advisory_ghsa_id"] not in accepted
    ]


def summarise(changes: list[Change]) -> str:
    """Name what the graph recognised, so an empty review is not read as a pass."""
    added = introduced(changes)
    if not added:
        return "No dependency additions were recognised in the supported manifests."
    lines = [f"Recognised {len(added)} added dependencies:"]
    lines += [
        f"  {change['ecosystem']} {change['name']} {change['version']}"
        f" ({change['manifest']}, advisories: {len(change['vulnerabilities'])})"
        for change in sorted(added, key=lambda change: (change["ecosystem"], change["name"]))
    ]
    return "\n".join(lines)


def compare(repository: str, base: str, head: str) -> list[Change]:
    """Read the dependency graph comparison with gh's authentication."""
    result = subprocess.run(
        ["gh", "api", f"repos/{repository}/dependency-graph/compare/{base}...{head}"],
        capture_output=True,
        text=True,
        check=True,
        timeout=60,
    )
    return cast("list[Change]", json.loads(result.stdout))


def review(changes: list[Change]) -> bool:
    """Print the recognised additions and every blocking advisory."""
    print(summarise(changes))
    blocked = blocking(changes)
    for change, advisory in blocked:
        print(
            f"BLOCKED {change['name']} {change['version']}: "
            f"{advisory['severity']} {advisory['advisory_ghsa_id']} "
            f"{advisory['advisory_summary']} ({advisory['advisory_url']})"
        )
    return not blocked


def main() -> None:
    """Review a pull request, or a recorded fixture, against the same policy."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default=os.environ.get("GITHUB_BASE_REF", ""))
    parser.add_argument("--head", default=os.environ.get("GITHUB_SHA", ""))
    # A recorded comparison exercises the policy without waiting for a real
    # advisory to appear in a real pull request.
    parser.add_argument("--changes", type=Path)
    arguments = parser.parse_args()
    if arguments.changes is not None:
        changes = cast("list[Change]", json.loads(arguments.changes.read_text(encoding="utf-8")))
    else:
        repository = os.environ["GITHUB_REPOSITORY"]
        changes = compare(repository, arguments.base, arguments.head)
    print(
        "Coverage: GitHub's dependency graph resolves the pinned GitHub Actions"
        " in this repository. The debian/control build dependencies, the CMake"
        " and KDE Frameworks interfaces, the pre-commit hook versions and the"
        " vendored shaders are not represented and are not reviewed here."
    )
    if not review(changes):
        message = "Introduced dependencies carry advisories at or above the policy threshold"
        raise SystemExit(message)


if __name__ == "__main__":
    main()
