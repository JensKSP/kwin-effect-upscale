#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the configured checks inside a maintained project container."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
from pathlib import Path

from ci_scope import changed_files, scope

# Written by the maintained Containerfiles from the debian/control they
# installed. Absent outside those images, where there is nothing to compare.
DEPENDENCY_STAMP = Path("/etc/upscale-dependency-stamp")


def verify_container_dependencies(root: Path) -> None:
    """Refuse to check in a container built from a different debian/control.

    A cached image that predates a build dependency does not announce itself.
    It fails much later, in a configure or compile step, as a missing package
    or a missing header, and the message names neither the image nor the
    dependency. Both maintained images have done exactly that. Comparing what
    the image was built from against what the tree now asks for turns that into
    one sentence naming the cause.
    """
    if not DEPENDENCY_STAMP.is_file():
        return
    current = hashlib.sha256((root / "debian" / "control").read_bytes()).hexdigest()
    if DEPENDENCY_STAMP.read_text().split()[0] != current:
        message = (
            "This container was built from a different debian/control, so its "
            "installed build dependencies no longer match the tree. Rebuild it, "
            "for example:\n"
            "    podman build --pull --build-arg DEPENDENCY_EPOCH=$(date -u +%Y-%m-%d) \\\n"
            "        -t upscale-check:trixie -f containers/trixie/Containerfile ."
        )
        raise SystemExit(message)


def run(*command: str) -> None:
    """Stop immediately on a failed build or check."""
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def hook(name: str, stage: str = "manual") -> None:
    """Use the repository's check definitions without duplicating their commands."""
    run("pre-commit", "run", name, "--all-files", "--hook-stage", stage)


def check(mode: str) -> None:
    """Run one CI job, retaining its separate build directory and reports."""
    build = Path("build") / mode
    build.mkdir(parents=True, exist_ok=True)
    os.environ["UPSCALE_BUILD_DIR"] = str(build.resolve())
    definitions = [
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_COMPILE_WARNING_AS_ERROR=ON",
        "-DBUILD_TESTING=ON",
        f"-DCMAKE_CXX_COMPILER={'g++' if mode in ('gcc', 'coverage') else 'clang++'}",
    ]
    if shutil.which("ccache"):
        definitions.append("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
    if mode == "coverage":
        # A cached workspace must not credit executions from an older test suite.
        for counter in build.rglob("*.gcda"):
            counter.unlink()
        definitions.append("-DUPSCALE_COVERAGE=ON")
    if mode in ("address", "thread"):
        sanitizer = "address,undefined" if mode == "address" else "thread"
        definitions.append(f"-DUPSCALE_SANITIZER={sanitizer}")
        definitions.append(f"-DUPSCALE_FUZZING={'ON' if mode == 'address' else 'OFF'}")
    run("cmake", "-S", ".", "-B", str(build), "-G", "Ninja", *definitions)
    # Ninja schedules the build; CMAKE_BUILD_PARALLEL_LEVEL remains an override.
    run("cmake", "--build", str(build))
    if mode == "tidy":
        run(
            "run-clang-tidy",
            "-p",
            str(build),
            "-quiet",
            "^" + re.escape(str(Path("src").resolve())) + "/",
        )
        run("python3", "-B", "tools/check-plugin-metadata.py")
        return
    hook("upscale-render-tests")
    if mode == "coverage":
        hook("upscale-coverage")
    elif mode == "address":
        hook("upscale-fuzz")
    elif mode in ("gcc", "clang"):
        os.environ["DESTDIR"] = str((build / "stage").resolve())
        run("cmake", "--install", str(build))
        del os.environ["DESTDIR"]


def main() -> None:
    """Provide a complete local run and the same individually selectable CI jobs."""
    modes = ("lint", "gcc", "clang", "tidy", "coverage", "address", "thread")
    parser = argparse.ArgumentParser(description=__doc__)
    selectable = (*modes, "docs", "codeql", "all")
    parser.add_argument("mode", choices=selectable, default="all", nargs="?")
    parser.add_argument("--base", default=os.environ.get("UPSCALE_CHECK_BASE", ""))
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)
    verify_container_dependencies(root)
    # Scope Git ownership trust to this process tree, not a user's global config.
    index = int(os.environ.get("GIT_CONFIG_COUNT", "0"))
    os.environ.update(
        {
            "GIT_CONFIG_COUNT": str(index + 1),
            f"GIT_CONFIG_KEY_{index}": "safe.directory",
            f"GIT_CONFIG_VALUE_{index}": str(root),
            "PRE_COMMIT_HOME": str(root / "build/pre-commit"),
            "CCACHE_DIR": str(root / "build/ccache"),
            "PYTHONDONTWRITEBYTECODE": "1",
            "TMPDIR": str(root / "build/tmp"),
        }
    )
    Path(os.environ["TMPDIR"]).mkdir(parents=True, exist_ok=True)
    for mode in modes if arguments.mode == "all" else (arguments.mode,):
        if mode == "codeql":
            # Through the same check definition as every other analysis here.
            hook("upscale-codeql")
        elif mode in ("lint", "docs"):
            selection = ["--all-files"]
            if mode == "docs":
                paths = changed_files(arguments.base)
                if scope(paths, pull_request=True)["build"] != "false":
                    message = "Documentation checks require a nonempty documentation-only diff"
                    raise ValueError(message)
                # Prefix paths so a filename beginning with '-' is never an option.
                selection = ["--files", *(f"./{path}" for path in paths)]
            run("pre-commit", "run", *selection, "--show-diff-on-failure")
            run("pre-commit", "run", *selection, "--hook-stage", "pre-push")
        else:
            check(mode)


if __name__ == "__main__":
    main()
