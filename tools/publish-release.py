#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Upload and verify a draft before making a release visible."""

import argparse
import json
import os
import subprocess
import tempfile
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
            message = "Uploaded release contents differ from the validated artifacts"
            raise ValueError(message)


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
    # The old nightly remains downloadable until the replacement's assets have
    # been uploaded and checked. A failure before here leaves it untouched.
    if nightly and tag in releases:
        run("gh", "release", "delete", tag, "--cleanup-tag", "--yes")
    run("gh", "release", "edit", draft, "--tag", tag, "--target", commit, "--draft=false")


if __name__ == "__main__":
    main()
