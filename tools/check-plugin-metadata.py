#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the effect's KPluginMetaData file against KDE's schema.

This is what ECM's JSON_SCHEMA commit hook does. It is not a pre-commit hook
here because the schema comes from libkf6coreaddons-data and the linting job
does not install KDE Frameworks; it runs in the container and in the CI jobs
that build, where the schema is present by definition.

A wrong key or a misspelled category in that file makes KWin drop the effect
without saying why, so this is worth a check of its own.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_SCHEMA = Path("/usr/share/kf6/jsonschema/kpluginmetadata.schema.json")


def metadata_files() -> list[Path]:
    """List the tracked JSON files that are plugin metadata.

    Plugin metadata is a JSON object with a KPlugin member at its root, which
    is the same rule ECM's hook applies. A file that is not JSON at all is not
    metadata either, so it is skipped rather than reported.
    """
    completed = subprocess.run(
        ("git", "ls-files", "*.json"),
        capture_output=True,
        text=True,
        check=True,
    )

    found = []
    for name in completed.stdout.splitlines():
        path = Path(name)
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            continue
        if isinstance(document, dict) and "KPlugin" in document:
            found.append(path)
    return found


def main() -> int:
    """Validate every metadata file and return the validator's exit status."""
    schema = Path(os.environ.get("KPLUGINMETADATA_SCHEMA", DEFAULT_SCHEMA))

    validator = shutil.which("check-jsonschema")
    if validator is None:
        print("error: check-jsonschema is not installed", file=sys.stderr)
        print(
            "       pipx install check-jsonschema, or run this in containers/trixie",
            file=sys.stderr,
        )
        return 1

    if not schema.is_file():
        print(f"error: no KPluginMetaData schema at {schema}", file=sys.stderr)
        print(
            "       install libkf6coreaddons-data, or set KPLUGINMETADATA_SCHEMA",
            file=sys.stderr,
        )
        return 1

    found = metadata_files()
    if not found:
        print("error: no KPluginMetaData file found; this project ships one", file=sys.stderr)
        return 1

    print(f"Validating against {schema}:")
    for path in found:
        print(f"  {path}")
    # The validator writes to the same log, and our own output is block
    # buffered as soon as that log is a pipe rather than a terminal.
    sys.stdout.flush()

    completed = subprocess.run(
        (validator, "--schemafile", str(schema), *(str(path) for path in found)),
        check=False,
    )
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
