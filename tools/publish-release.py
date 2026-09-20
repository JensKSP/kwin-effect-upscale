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

import ci_targets
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


# The order a person reads them in, which is not the order a file listing
# produces. Debian Trixie on a 64-bit PC is the one build with an acceptance
# machine behind it, so it comes first; everything below it is offered in
# descending order of how likely someone is to want it.
INSTALLATION = (
    ("trixie_amd64.deb", "Debian Trixie", "64-bit PC (amd64)", "sudo apt install ./{name}"),
    ("trixie_arm64.deb", "Debian Trixie", "ARM64", "sudo apt install ./{name}"),
    ("resolute_amd64.deb", "Kubuntu 26.04 LTS", "64-bit PC (amd64)", "sudo apt install ./{name}"),
    ("resolute_arm64.deb", "Kubuntu 26.04 LTS", "ARM64", "sudo apt install ./{name}"),
    (".fc*.x86_64.rpm", "Fedora", "64-bit PC (x86_64)", "sudo dnf install ./{name}"),
    (".fc*.aarch64.rpm", "Fedora", "ARM64 (aarch64)", "sudo dnf install ./{name}"),
    ("!.x86_64.rpm", "openSUSE Tumbleweed", "64-bit PC (x86_64)", "sudo zypper install ./{name}"),
    ("!.aarch64.rpm", "openSUSE Tumbleweed", "ARM64 (aarch64)", "sudo zypper install ./{name}"),
    ("x86_64.pkg.tar.zst", "Arch", "64-bit PC (x86_64)", "sudo pacman -U ./{name}"),
    # Last, and matched on "amd64.pkg" alone: FreeBSD names its package after
    # the manifest, with no distribution stamp and no debug companion.
    ("amd64.pkg", "FreeBSD", "64-bit PC (amd64)", "sudo pkg add ./{name}"),
)


# The stable download names, which are copies of packages already in the table.
# Matched by name rather than by shape: an Arch or FreeBSD alias ends exactly as
# the package it copies does, so a shape cannot tell them apart, and the table
# would offer the same package twice. The README links to these; the release
# notes name the versioned file, which says what it is.
ALIASES = frozenset(
    ci_targets.download_name(entry.identifier, architecture)
    for entry in ci_targets.TARGETS
    for architecture in entry.architectures
)


def wanted(name: str, shape: str) -> bool:
    """Match one installable package, never its debug, source or stable copy."""
    if name in ALIASES:
        return False
    if any(part in name for part in ("-debuginfo-", "-debugsource-", "-debug-", "-dbgsym_")):
        return False
    if name.endswith((".src.rpm", ".src.tar.gz", ".dsc", ".tar.xz", ".buildinfo", ".changes")):
        return False
    # "!" marks openSUSE, whose names carry no distribution stamp at all and
    # would otherwise also match Fedora's.
    if shape.startswith("!"):
        return name.endswith(shape[1:]) and ".fc" not in name
    if "*" in shape:
        head, tail = shape.split("*", 1)
        return head in name and name.endswith(tail)
    return name.endswith(shape)


# The header plus the table's two rules: a guide with nothing under them has
# found no installable package and is not worth writing.
GUIDE_HEADER_LINES = 4


def installation_guide(repository: str, tag: str, names: list[str]) -> str:
    """Write the part of the release notes a person installing this reads.

    A release carries five distributions, two architectures, debug symbols,
    source packages and Debian's build records. That is a long list to face
    when the question is "which one do I download", so the few files that
    answer it are named first and the rest is folded away.
    """
    base = f"https://github.com/{repository}/releases/download/{tag}"
    rows = [
        "## Which file do I need?",
        "",
        "| System | Architecture | Package |",
        "| --- | --- | --- |",
    ]
    listed: set[str] = set()
    # One command per package manager, keyed by the command itself so the four
    # Debian rows contribute one apt line between them, and in the order the
    # table lists them. Keeping only the first command of all would print apt
    # to a reader who came for the Fedora or Arch package.
    install: dict[str, str] = {}
    for shape, system, architecture, command in INSTALLATION:
        found = sorted(name for name in names if wanted(name, shape))
        for name in found:
            listed.add(name)
            rows.append(f"| {system} | {architecture} | [{name}]({base}/{name}) |")
            install.setdefault(command, command.format(name=name))
    if len(rows) == GUIDE_HEADER_LINES:
        return ""
    rows.extend(
        (
            "",
            "Install a downloaded package with your own package manager:",
            "",
            "```bash",
            *install.values(),
            "```",
            "",
        )
    )
    rows.append(
        "Each package depends on the exact KWin it was built against, so it "
        "refuses to install against a different one rather than letting the "
        "compositor load a plugin built for another ABI. After a KWin upgrade, "
        "take the matching build."
    )
    rest = sorted(set(names) - listed)
    if rest:
        rows.extend(
            (
                "",
                "<details>",
                (
                    "<summary>Stable download names, debug symbols, source"
                    " packages, build records and checksums</summary>"
                ),
                "",
            )
        )
        rows.extend(f"- [{name}]({base}/{name})" for name in rest)
        rows.extend(("", "</details>"))
    return "\n".join(rows)


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
    # Written after the upload, because it links the assets by name, and
    # against the final tag rather than the staging one a nightly is built in.
    guide = installation_guide(
        repository, tag, [path.name for path in arguments.directory.iterdir()]
    )
    if guide:
        body = run("gh", "release", "view", draft, "--json", "body", "--jq", ".body")
        with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False) as notes:
            notes.write(guide + ("\n\n" + body if body else "\n"))
            written = notes.name
        run("gh", "release", "edit", draft, "--notes-file", written)
        Path(written).unlink()
    # The old nightly remains downloadable until the replacement's assets have
    # been uploaded and checked. A failure before here leaves it untouched.
    if nightly and tag in releases:
        run("gh", "release", "delete", tag, "--cleanup-tag", "--yes")
    promote(repository, release_id, tag, commit)


if __name__ == "__main__":
    main()
