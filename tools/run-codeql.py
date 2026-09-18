#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the CodeQL analysis this repository is scanned with, locally and in CI."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
from collections import Counter
from pathlib import Path
from typing import NotRequired, TypedDict, cast


# Only the part of SARIF this report reads: which rule produced a result, and
# what severity that rule declares for itself. A result is left as a plain
# mapping, because its one interesting key is spelled in SARIF's own style.
class Rule(TypedDict):
    """One query, as the tool that ran it describes itself in its report."""

    id: str
    properties: NotRequired[dict[str, object]]


class Driver(TypedDict):
    """The analysis tool's own description of the queries it ran."""

    rules: NotRequired[list[Rule]]


class Tool(TypedDict):
    """SARIF nests the driver inside the tool that drove it."""

    driver: Driver


class Run(TypedDict):
    """One language's analysis, which is one run in the report."""

    tool: Tool
    results: NotRequired[list[dict[str, str]]]


class Report(TypedDict):
    """A SARIF file as CodeQL writes it."""

    runs: list[Run]


# What this repository actually ships: the KWin effect, the Python that builds,
# checks and publishes it, and the workflows that run with a repository token.
# GitHub reports the same three languages for this repository.
LANGUAGES = ("c-cpp", "python", "actions")

# CodeQL spells the C++ language "c-cpp" and its query pack "cpp". For the other
# two, one name serves both.
PACKS = {"c-cpp": "cpp"}

# The scan starts with the suite GitHub's own default setup runs, so the
# findings are the ones a reader of the Security tab expects. The wider suites
# are selectable while their additional findings are being assessed; they are
# not enabled by default before anyone has read them.
SUITES = ("code-scanning", "security-extended", "security-and-quality")

# The CLI is pinned like every hook version in this repository, so a scan cannot
# start reporting differently because an upstream release moved. To raise it,
# take the release tag and its published checksum from
# https://github.com/github/codeql-action/releases and run a full scan before
# committing the new pair.
BUNDLE = "codeql-bundle-v2.27.0"
BUNDLE_FILE = "codeql-bundle-linux64.tar.gz"
BUNDLE_SHA256 = "8e870433e5c80d0e916c3c1aa9005fc88aab990bcdcc649fade9dfc4d7e94305"
BUNDLE_URL = f"https://github.com/github/codeql-action/releases/download/{BUNDLE}/{BUNDLE_FILE}"

# An interpreted-language extractor reads whatever it finds below the source
# root, and below `build/` sit the traced build, the check caches and the
# unpacked CodeQL bundle itself. Naming the directory that actually holds each
# language keeps a scan of this repository from becoming a scan of its tools.
# C++ has no entry: there, the compilations the extractor observes decide, and
# its generated inputs are wanted.
PATHS = {"python": ("tools",), "actions": (".github",)}


def pack(language: str) -> str:
    """Name the query pack for a language CodeQL spells differently."""
    return PACKS.get(language, language)


def configuration(language: str, output: Path) -> Path | None:
    """Write the path filter for a language that has one, and name the file."""
    paths = PATHS.get(language)
    if paths is None:
        return None
    output.mkdir(parents=True, exist_ok=True)
    path = output / f"{language}-paths.yml"
    path.write_text("paths:\n" + "".join(f"  - {entry}\n" for entry in paths), encoding="utf-8")
    return path


def create(
    codeql: str, language: str, database: Path, build: Path, config: Path | None
) -> list[str]:
    """Extract one language; only C++ needs a compilation for CodeQL to observe."""
    command = [
        codeql,
        "database",
        "create",
        str(database),
        f"--language={language}",
        "--source-root=.",
        "--overwrite",
        "--threads=0",
    ]
    if config is not None:
        command.append(f"--codescanning-config={config}")
    if language != "c-cpp":
        return command
    # The maintained CMake and Ninja build rather than CodeQL's autobuild guess,
    # which cannot know this project configures against KWin and KDE Frameworks.
    # A build directory of its own keeps the traced compilation apart from the
    # check builds. Warnings are not errors here: the compiler jobs own that
    # verdict, and a warning from a flag the extractor injects must not abort a
    # security scan.
    configure = f"cmake -S . -B {build} -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON"
    command.append(f"--command={configure}")
    command.append(f"--command=cmake --build {build}")
    return command


def analyze(codeql: str, language: str, database: Path, sarif: Path, suite: str) -> list[str]:
    """Run a suite and write SARIF that both the Security tab and a reader can use."""
    name = pack(language)
    return [
        codeql,
        "database",
        "analyze",
        str(database),
        f"codeql/{name}-queries:codeql-suites/{name}-{suite}.qls",
        "--format=sarif-latest",
        f"--output={sarif}",
        # Without a category, each uploaded language replaces the previous one.
        f"--sarif-category={language}",
        "--threads=0",
    ]


def severity(rule: Rule) -> str:
    """Report the severity a rule carries, rather than inventing one it lacks."""
    properties = rule.get("properties")
    if not isinstance(properties, dict):
        return "unknown"
    for key in ("security-severity", "problem.severity"):
        if properties.get(key) is not None:
            return str(properties[key])
    return "unknown"


def findings(report: Report) -> Counter[str]:
    """Count results by severity, so that a scan which found nothing says so."""
    rules: dict[str, str] = {}
    counted: Counter[str] = Counter()
    for analysis in report["runs"]:
        for rule in analysis["tool"]["driver"].get("rules", []):
            rules[rule["id"]] = severity(rule)
        for result in analysis.get("results", []):
            counted[rules.get(result.get("ruleId", ""), "unknown")] += 1
    return counted


def existing(selected: str | None) -> str | None:
    """Find a CLI already present: an explicit path, CodeQL's variable, PATH."""
    distribution = os.environ.get("CODEQL_DIST")
    candidates = [selected, f"{distribution}/codeql" if distribution else None, "codeql"]
    for candidate in candidates:
        if candidate and (resolved := shutil.which(candidate)) is not None:
            return resolved
    return None


def executable(selected: str | None, directory: Path) -> str:
    """Use the CLI that is already there, or fetch the pinned bundle once."""
    found = existing(selected)
    if found is not None:
        return found
    if selected is not None:
        message = f"{selected} is not an executable CodeQL command line"
        raise SystemExit(message)
    return str(install(directory) / "codeql")


# What is unpacked into the bundle directory cannot say which release it came
# from, so it is recorded next to it. Without that, raising the pin would leave
# the scan running on the command line a previous pin had already unpacked.
MARKER = "pinned-bundle"


def installed(directory: Path) -> str:
    """Name the bundle unpacked here, or nothing when none is complete."""
    marker = directory / MARKER
    if not (directory / "codeql" / "codeql").exists() or not marker.exists():
        return ""
    return marker.read_text(encoding="utf-8").strip()


def install(directory: Path) -> Path:
    """Fetch the pinned bundle once, and refuse anything but the pinned bytes."""
    distribution = directory / "codeql"
    pinned = f"{BUNDLE} {BUNDLE_SHA256}"
    present = installed(directory)
    if present == pinned:
        print(f"Using {BUNDLE} already unpacked in {distribution}")
        return distribution
    if present:
        print(f"Replacing {present.split()[0]}, which is no longer the pinned bundle")
    shutil.rmtree(distribution, ignore_errors=True)
    (directory / MARKER).unlink(missing_ok=True)
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / BUNDLE_FILE
    if not BUNDLE_URL.startswith("https://github.com/github/codeql-action/releases/download/"):
        message = "The bundle URL must stay on GitHub's own release downloads"
        raise SystemExit(message)
    fetch = ["curl", "--fail", "--silent", "--show-error", "--location"]
    run([*fetch, "--output", str(archive), BUNDLE_URL])
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != BUNDLE_SHA256:
        message = f"{BUNDLE_FILE} has digest {digest}, not the pinned {BUNDLE_SHA256}"
        raise SystemExit(message)
    with tarfile.open(archive) as bundle:
        bundle.extractall(directory, filter="data")
    archive.unlink()
    # Only after a complete extraction, so an interrupted one is not trusted.
    (directory / MARKER).write_text(pinned + "\n", encoding="utf-8")
    return distribution


def run(command: list[str]) -> None:
    """Stop on the first failed extraction or analysis, showing what was run."""
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def scan(codeql: str, languages: tuple[str, ...], suite: str, output: Path) -> Counter[str]:
    """Produce one database and one SARIF file per language below `output`."""
    output.mkdir(parents=True, exist_ok=True)
    total: Counter[str] = Counter()
    for language in languages:
        database = output / f"{language}-database"
        sarif = output / f"{language}.sarif"
        config = configuration(language, output)
        run(create(codeql, language, database, output / f"{language}-build", config))
        run(analyze(codeql, language, database, sarif, suite))
        counted = findings(cast("Report", json.loads(sarif.read_text(encoding="utf-8"))))
        total.update(counted)
        print(f"{language}: {sum(counted.values())} results {dict(sorted(counted.items()))}")
    return total


def main() -> None:
    """Scan the configured languages and report what was found, without hiding it."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", action="append", choices=LANGUAGES, dest="languages")
    # The environment carries the selection through pre-commit, which is how
    # this and the other manual-stage checks are run.
    parser.add_argument(
        "--suite", choices=SUITES, default=os.environ.get("UPSCALE_CODEQL_SUITE") or SUITES[0]
    )
    parser.add_argument("--output", default=Path("build/codeql"), type=Path)
    parser.add_argument("--codeql")
    # Where the pinned bundle is kept when no command line is installed already.
    # The first run fetches it; later runs and CI reuse what is there.
    parser.add_argument("--install", default=Path("build/codeql-cli"), type=Path)
    # Findings are assessed before they gate a pull request. Until that has
    # happened the scan publishes them instead of failing on the first one.
    parser.add_argument("--fail-on-findings", action="store_true")
    arguments = parser.parse_args()
    os.chdir(Path(__file__).resolve().parent.parent)
    codeql = executable(arguments.codeql, arguments.install)
    total = scan(codeql, tuple(arguments.languages or LANGUAGES), arguments.suite, arguments.output)
    print(f"CodeQL {arguments.suite}: {sum(total.values())} results in total")
    if total and arguments.fail_on_findings:
        sys.exit(1)


if __name__ == "__main__":
    main()
