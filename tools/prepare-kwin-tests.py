#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy packaged KWin sources and adapt only integration-test startup.

Run in containers/wayland-tests. Test bodies and production KWin sources are
unchanged. The prepared source is disposable build output, never a checkout
whose upstream changes could be mistaken for changes to this effect.
"""

import argparse
import shutil
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    """Fail closed if the pinned upstream test harness has changed."""
    source = path.read_text()
    if source.count(old) != 1:
        message = f"expected exactly one adapter location in {path}: {old!r}"
        raise ValueError(message)
    path.write_text(source.replace(old, new))


def copy_changed(source: Path, destination: Path) -> None:
    """Keep unchanged framework headers from rebuilding every upstream binary."""
    if not destination.exists() or source.read_bytes() != destination.read_bytes():
        shutil.copyfile(source, destination)


def main() -> None:
    """Prepare a fresh source copy without touching an existing build."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", default=Path("build/wayland-conformance/source"), type=Path)
    parser.add_argument(
        "--refresh", action="store_true", help="refresh this project's adapter and added test"
    )
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    if not args.out.resolve().is_relative_to(project / "build"):
        parser.error("prepared sources must be under this repository's build directory")
    if args.out.exists() and not args.refresh:
        parser.error("output already exists; reuse it or choose a fresh build directory")
    if not args.refresh:
        shutil.copytree(args.source, args.out)
    integration = args.out / "autotests/integration"
    copy_changed(project / "autotests/kwin_conformance.h", integration / "kwin_conformance.h")
    copy_changed(project / "autotests/kwin_scaling_test.cpp", integration / "kwin_scaling_test.cpp")
    application = integration / "kwin_wayland_test.cpp"
    if not args.refresh:
        replace_once(
            application,
            '#include "kwin_wayland_test.h"',
            '#include "kwin_wayland_test.h"\n#include "kwin_conformance.h"',
        )
        replace_once(
            application,
            "    compositor->start();",
            "    compositor->start();\n    loadUpscaleConformance();",
        )
    # Options reads KWIN_COMPOSE while it is constructed, before the scene
    # starts. Setting the variable at compositor->start() is already too late.
    previous = "    configureUpscaleConformance();\n    compositor->start();"
    if previous in application.read_text():
        replace_once(application, previous, "    compositor->start();")
    if "    configureUpscaleConformance();\n    createOptions();" not in application.read_text():
        replace_once(
            application,
            "    createOptions();",
            "    configureUpscaleConformance();\n    createOptions();",
        )
    registration = integration / "CMakeLists.txt"
    if "testUpscaleProduction" not in registration.read_text():
        with registration.open("a") as stream:
            stream.write(
                "\nintegrationTest(NAME testUpscaleProduction SRCS kwin_scaling_test.cpp)\n"
                "qt6_generate_wayland_protocol_client_sources(testUpscaleProduction\n"
                "    FILES ${WaylandProtocols_DATADIR}/stable/viewporter/viewporter.xml)\n"
            )
    print(f"Prepared upstream test harness in {args.out}")


if __name__ == "__main__":
    main()
