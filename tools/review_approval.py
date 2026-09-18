# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Publish approval status from authenticated GitHub reviews, never PR code."""

import argparse
import json
import os
import subprocess
from typing import TypedDict, cast


class Reviewer(TypedDict):
    """GitHub's authenticated identity, not text written in a review body."""

    id: int
    login: str
    type: str


class Review(TypedDict):
    """Only decision metadata is relevant to this gate."""

    id: int
    user: Reviewer
    state: str
    commit_id: str


class Revision(TypedDict):
    """A GitHub pull-request revision."""

    sha: str


class PullMetadata(TypedDict):
    """Current pull-request metadata fetched from the base repository."""

    number: int
    state: str
    head: Revision


# An arbitrary user naming CodeRabbit in an approval is not the installed app.
# Verified against this repository's CodeRabbit reviews on 2026-09-18.
CODERABBIT_ID = 136622811
CONTEXT = "CodeRabbit approval"


def approved(reviews: list[Review], head: str) -> bool:
    """Require the bot's latest substantive decision to approve this exact head."""
    decisions = [
        review
        for review in reviews
        if review["user"]["id"] == CODERABBIT_ID
        and review["user"]["type"] == "Bot"
        and review["state"] in {"APPROVED", "CHANGES_REQUESTED", "DISMISSED"}
    ]
    if not decisions:
        return False
    latest = max(decisions, key=lambda review: review["id"])
    return latest["state"] == "APPROVED" and latest["commit_id"] == head


def api(endpoint: str, payload: dict[str, str] | None = None) -> object:
    """Use gh authentication without exposing credentials or evaluating a shell."""
    command = ["gh", "api", endpoint]
    if payload is not None:
        command.extend(["--method", "POST", "--input", "-"])
    result = subprocess.run(
        command,
        input=json.dumps(payload) if payload is not None else None,
        capture_output=True,
        text=True,
        check=True,
        timeout=60,
    )
    return cast("object", json.loads(result.stdout))


def pages(endpoint: str) -> list[object]:
    """Read every page; an approval or dismissal beyond page one still matters."""
    result = subprocess.run(
        ["gh", "api", "--paginate", "--slurp", endpoint],
        capture_output=True,
        text=True,
        check=True,
        timeout=60,
    )
    return [item for page in cast("list[list[object]]", json.loads(result.stdout)) for item in page]


def publish(repository: str, number: int, head: str, state: str) -> None:
    """Record approval in the required context, not merely review completion."""
    api(
        f"repos/{repository}/statuses/{head}",
        {
            "context": CONTEXT,
            "state": state,
            "description": (
                "CodeRabbit approved this commit"
                if state == "success"
                else "Waiting for CodeRabbit approval of this commit"
            ),
            "target_url": f"https://github.com/{repository}/pull/{number}",
        },
    )


def refresh(repository: str, number: int, head: str) -> bool:
    """Apply reviews only to an open PR whose head still matches the snapshot."""
    endpoint = f"repos/{repository}/pulls/{number}"
    pull = cast("PullMetadata", api(endpoint))
    if pull["state"] != "open" or pull["head"]["sha"] != head:
        return False
    reviews = cast("list[Review]", pages(f"{endpoint}/reviews?per_page=100"))
    success = approved(reviews, head)
    current = cast("PullMetadata", api(endpoint))
    if current["state"] != "open" or current["head"]["sha"] != head:
        # Never transfer a previous head's approval to newly pushed code.
        return False
    print(f"PR #{number} ({head}): {'approved' if success else 'awaiting approval'}")
    return success


def refresh_all(repository: str, *, write: bool) -> None:
    """Combine same-commit PRs: a commit status cannot distinguish their numbers."""
    pulls = cast("list[PullMetadata]", pages(f"repos/{repository}/pulls?state=open&per_page=100"))
    groups: dict[str, list[int]] = {}
    for pull in pulls:
        groups.setdefault(pull["head"]["sha"], []).append(pull["number"])
    # Clear every known head before reading reviews. If a subsequent API call
    # fails, no previously successful verdict survives for those heads.
    if write:
        for head, numbers in groups.items():
            publish(repository, numbers[0], head, "pending")
    for head, numbers in groups.items():
        results = [refresh(repository, number, head) for number in numbers]
        if write and all(results):
            publish(repository, numbers[0], head, "success")


def main() -> None:
    """Refresh open PRs after PR/review events, or inspect them without writing."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publish", action="store_true")
    arguments = parser.parse_args()
    repository = os.environ["GITHUB_REPOSITORY"]
    # Fork review workflow_run payloads can omit their PR association. Reading
    # open PRs also makes delayed events harmless: event data is never a verdict.
    refresh_all(repository, write=arguments.publish)


if __name__ == "__main__":
    main()
