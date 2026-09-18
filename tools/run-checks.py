#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the configured checks inside a maintained project container."""

import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path


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
    parser.add_argument("mode", choices=(*modes, "all"), default="all", nargs="?")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)
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
        if mode == "lint":
            run("pre-commit", "run", "--all-files", "--show-diff-on-failure")
            run("pre-commit", "run", "--all-files", "--hook-stage", "pre-push")
        else:
            check(mode)


if __name__ == "__main__":
    main()
