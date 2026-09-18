# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run bounded libFuzzer exploration, retaining the corpus and crash inputs."""

import os
import struct
import subprocess
from pathlib import Path


def main() -> int:
    """Seed boundaries before mutation; every crash or timeout fails the hook."""
    build = Path(os.environ.get("UPSCALE_BUILD_DIR", "build")).resolve()
    corpus = build / "fuzz-corpus"
    artifacts = build / "fuzz-artifacts"
    corpus.mkdir(exist_ok=True)
    artifacts.mkdir(exist_ok=True)
    # One job per native worker (libFuzzer defaults to half the CPU cores).
    # The tool owns worker creation, shared-corpus reload and failure reporting.
    jobs = max(1, (os.cpu_count() or 1) // 2)
    for index, dimensions in enumerate(
        [(1920, 1080, 3840, 2160), (0, -1, 1, 1), (2**30, 2**30, 2**31 - 1, 2**31 - 1)]
    ):
        for preset in range(7):
            (corpus / f"seed-{index}-{preset}").write_bytes(
                struct.pack("<6i", *dimensions, preset, 50)
            )
    return subprocess.call(
        [
            str(build / "bin/upscale_resolution_fuzz"),
            str(corpus),
            f"-jobs={jobs}",
            f"-artifact_prefix={artifacts}/",
            f"-max_total_time={os.environ.get('UPSCALE_FUZZ_SECONDS', '60')}",
            "-timeout=10",
            "-rss_limit_mb=2048",
            "-max_len=24",
        ],
        cwd=build,
    )


if __name__ == "__main__":
    raise SystemExit(main())
