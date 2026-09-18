# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Select CI work conservatively; unknown changes retain the complete checks."""

import json
import os
import subprocess
from pathlib import PurePosixPath


def documentation(path: str) -> bool:
    """Recognize inert documentation, never a directory-wide code exclusion."""
    name = PurePosixPath(path)
    return name.suffix == ".md" and (len(name.parts) == 1 or name.parts[0] in {"doc", ".github"})


def changed_files(base: str, head: str | None = None) -> list[str]:
    """Include both sides of renames and deleted paths, preserving odd filenames."""
    if not base or base.startswith("-") or (head is not None and head.startswith("-")):
        message = "A valid base and head revision are required"
        raise ValueError(message)
    result = subprocess.run(
        ["git", "diff", "--no-renames", "--name-only", "-z", base, *([head] if head else []), "--"],
        check=True,
        capture_output=True,
    )
    return [os.fsdecode(path) for path in result.stdout.split(b"\0") if path]


def scope(paths: list[str], *, pull_request: bool, master_push: bool = False) -> dict[str, str]:
    """Limit proven documentation changes; publication and manual runs stay full."""
    docs_only = (
        (pull_request or master_push) and bool(paths) and all(documentation(path) for path in paths)
    )
    packaging = (
        pull_request
        and not docs_only
        and any(
            path == "CMakeLists.txt"
            or path.startswith(
                ("debian/", "containers/", "cmake/", "autotests/", "tools/", ".github/")
            )
            for path in paths
        )
    )
    return {
        "build": str(not docs_only).lower(),
        "packaging": str(packaging).lower(),
        "modes": json.dumps(["docs"] if docs_only else ["lint", "gcc", "clang", "tidy"]),
    }


def main() -> None:
    """Emit job outputs; unavailable comparisons never opt into reduced checks."""
    pull_request = os.environ.get("GITHUB_EVENT_NAME") == "pull_request"
    master_push = (
        os.environ.get("GITHUB_EVENT_NAME") == "push"
        and os.environ.get("GITHUB_REF") == "refs/heads/master"
    )
    paths: list[str] = []
    if pull_request or master_push:
        try:
            paths = changed_files(os.environ.get("BASE", ""))
        except (ValueError, subprocess.CalledProcessError):
            # An unknown comparison also needs packaging validation. The sentinel
            # cannot be classified as documentation and selects the smoke build.
            paths = ["tools/unknown-comparison"]
    for name, value in scope(paths, pull_request=pull_request, master_push=master_push).items():
        print(f"{name}={value}")


if __name__ == "__main__":
    main()
