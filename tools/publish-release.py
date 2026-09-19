#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Upload and verify a draft before making a release visible."""

import argparse
import json
import os
import shlex
import subprocess
import tempfile
import time
from pathlib import Path

from release_assets import digest, validate_version


def run(*command: str) -> str:
    """Propagate API failures; an authorization error is not a missing release."""
    return subprocess.check_output(command, text=True).strip()


def verify_download(tag: str, directory: Path) -> None:
    """Verify every uploaded byte, including the manifest and signing bundle."""
    with tempfile.TemporaryDirectory(dir="build", prefix="release-verify-") as temporary:
        run("gh", "release", "download", tag, "--dir", temporary)
        downloaded = {p.name: digest(p) for p in Path(temporary).iterdir()}
        expected = {p.name: digest(p) for p in directory.iterdir()}
        if downloaded != expected:
            missing = sorted(expected.keys() - downloaded.keys())
            unexpected = sorted(downloaded.keys() - expected.keys())
            changed = sorted(
                name
                for name in expected.keys() & downloaded.keys()
                if expected[name] != downloaded[name]
            )
            message = (
                "Uploaded release contents differ from the validated artifacts: "
                f"missing={missing}, unexpected={unexpected}, changed={changed}"
            )
            raise ValueError(message)


def promote(repository: str, release_id: int, tag: str, commit: str) -> None:
    """Retry the idempotent final update and report a precise recovery command."""
    # Address the release by ID: a successful update with a lost response has
    # already renamed the draft, so retrying by its former tag would fail.
    command = (
        "gh",
        "api",
        "--method",
        "PATCH",
        f"repos/{repository}/releases/{release_id}",
        "-f",
        f"tag_name={tag}",
        "-f",
        f"target_commitish={commit}",
        "-F",
        "draft=false",
    )
    attempts = 3
    for attempt in range(attempts):
        try:
            run(*command)
        except subprocess.CalledProcessError as error:
            if attempt == attempts - 1:
                message = (
                    "Release promotion failed after three attempts. "
                    "The assets were verified; retry promotion with: " + shlex.join(command)
                )
                raise RuntimeError(message) from error
            time.sleep(2**attempt)
        else:
            return


def main() -> None:
    """Keep published stable releases unchanged and stage nightly replacements."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", type=validate_version)
    parser.add_argument("channel", choices=("release", "nightly"))
    parser.add_argument("directory", type=Path)
    arguments = parser.parse_args()
    repository = os.environ["GITHUB_REPOSITORY"]
    commit = os.environ["GITHUB_SHA"]
    inventory = run(
        "gh",
        "api",
        f"repos/{repository}/releases",
        "--paginate",
        "--jq",
        ".[] | [.tag_name, .draft] | @json",
    )
    releases = dict(json.loads(line) for line in inventory.splitlines())
    nightly = arguments.channel == "nightly"
    tag = "nightly" if nightly else f"v{arguments.version}"
    draft = f"nightly-staging-{os.environ['GITHUB_RUN_ID']}" if nightly else tag
    if not nightly and tag in releases and not releases[tag]:
        verify_download(tag, arguments.directory)
        return
    title = f"{'Nightly' if nightly else 'kwin-effect-upscale'} {arguments.version}"
    if draft not in releases:
        command = [
            "gh",
            "release",
            "create",
            draft,
            "--draft",
            "--title",
            title,
            "--target",
            commit,
        ]
        if nightly:
            command.extend(["--prerelease", "--notes", f"Development build from commit {commit}."])
        else:
            command.extend(["--verify-tag", "--generate-notes"])
        run(*command)
    run(
        "gh",
        "release",
        "upload",
        draft,
        "--clobber",
        *(str(path) for path in sorted(arguments.directory.iterdir())),
    )
    verify_download(draft, arguments.directory)
    release_id = int(
        run("gh", "release", "view", draft, "--json", "databaseId", "--jq", ".databaseId")
    )
    # The old nightly remains downloadable until the replacement's assets have
    # been uploaded and checked. A failure before here leaves it untouched.
    if nightly and tag in releases:
        run("gh", "release", "delete", tag, "--cleanup-tag", "--yes")
    promote(repository, release_id, tag, commit)


if __name__ == "__main__":
    main()
