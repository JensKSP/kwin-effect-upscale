#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Copy Debian's glmark2 source, make it crash on request, and build it.

A game can disappear without warning, and the tests of what the effect does
then need a real one that does so at a moment they choose. This copies the
packaged glmark2 source, adds autotests/glmark2_crash.h and calls into it at a
few places, and builds its Wayland and X11 flavours. Nothing else changes, and
without UPSCALE_TEST_CRASH the build behaves as glmark2 does.

Run in containers/wayland-tests after `apt-get build-dep glmark2` and
`apt-get source glmark2`. The copy and the build are disposable build output
under build/, never a checkout whose upstream changes could be mistaken for
this project's.
"""

import argparse
import shutil
import subprocess
from pathlib import Path

HEADER = '#include "upscale-test-crash.h"\n'

# Where glmark2 2023.01 is taught to crash. Each anchor must occur exactly once;
# a changed upstream source fails here rather than building a game that never
# crashes and a test that then proves nothing.
INSERTIONS: dict[str, list[tuple[str, str]]] = {
    "src/native-state-wayland.cpp": [
        ('#include "log.h"\n', '#include "log.h"\n' + HEADER),
        (
            "    wl_display_roundtrip(display_->display);\n\n    setup_cursor();\n",
            (
                "    wl_display_roundtrip(display_->display);\n"
                '    if (upscaleTestCrashesAt("bind")) {\n'
                "        const my_output *told =\n"
                "            display_->outputs.empty() ? nullptr : display_->outputs.at(0);\n"
                '        upscaleTestCrash("bind", told ? told->width : 0,\n'
                "                         told ? told->height : 0);\n"
                "    }\n\n    setup_cursor();\n"
            ),
        ),
        (
            (
                "    while (window_->waiting_for_configure)\n"
                "        wl_display_roundtrip(display_->display);\n"
            ),
            (
                "    while (window_->waiting_for_configure)\n"
                "        wl_display_roundtrip(display_->display);\n"
                '    if (upscaleTestCrashesAt("window")) {\n'
                '        upscaleTestCrash("window", output ? output->width : 0,\n'
                "                         output ? output->height : 0);\n"
                "    }\n"
            ),
        ),
    ],
    "src/native-state-x11.cpp": [
        ('#include "log.h"\n', '#include "log.h"\n' + HEADER),
        (
            "    attr.event_mask = KeyPressMask;\n",
            (
                "    attr.event_mask = KeyPressMask"
                ' | (upscaleTestCrashesAt("resize") ? StructureNotifyMask : 0);\n'
            ),
        ),
        (
            "    XStoreName(xdpy_ , xwin_,  win_name);\n",
            (
                "    XStoreName(xdpy_ , xwin_,  win_name);\n"
                "    {\n"
                "        const long pid = getpid();\n"
                '        XChangeProperty(xdpy_, xwin_, XInternAtom(xdpy_, "_NET_WM_PID", False),\n'
                "                        XA_CARDINAL, 32, PropModeReplace,\n"
                "                        reinterpret_cast<const unsigned char *>(&pid), 1);\n"
                "    }\n"
            ),
        ),
        (
            "    XNextEvent(xdpy_, &event);\n",
            (
                "    XNextEvent(xdpy_, &event);\n"
                '    if (event.type == ConfigureNotify && upscaleTestCrashesAt("resize")\n'
                "        && (event.xconfigure.width != properties_.width\n"
                "            || event.xconfigure.height != properties_.height)) {\n"
                '        upscaleTestCrash("resize", event.xconfigure.width,\n'
                "                         event.xconfigure.height);\n"
                "    }\n"
            ),
        ),
    ],
    "src/canvas-generic.cpp": [
        ('#include "log.h"\n', '#include "log.h"\n' + HEADER),
        (
            "            native_state_.flip();\n",
            "            native_state_.flip();\n            upscaleTestCrashAfterFrame();\n",
        ),
    ],
}


def replace_once(path: Path, old: str, new: str) -> None:
    """Fail closed if the pinned upstream source has changed."""
    source = path.read_text()
    if source.count(old) != 1:
        message = f"expected exactly one crash point location in {path}: {old!r}"
        raise ValueError(message)
    path.write_text(source.replace(old, new))


def main() -> None:
    """Prepare a fresh crashing copy and build it."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--out", default=Path("build/test-games/glmark2-crash"), type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    if not args.out.resolve().is_relative_to(project / "build"):
        parser.error("the prepared game must be under this repository's build directory")
    if args.out.exists():
        parser.error("output already exists; remove it or choose a fresh directory")
    shutil.copytree(args.source, args.out)
    shutil.copyfile(project / "autotests/glmark2_crash.h", args.out / "src/upscale-test-crash.h")
    for relative, insertions in INSERTIONS.items():
        for old, new in insertions:
            replace_once(args.out / relative, old, new)
    build = args.out / "build"
    subprocess.run(
        ["meson", "setup", str(build), str(args.out), "-Dflavors=wayland-gl,x11-gl"], check=True
    )
    subprocess.run(["meson", "compile", "-C", str(build)], check=True)
    print(f"Crashing glmark2 in {build / 'src'}, data in {args.out / 'data'}")


if __name__ == "__main__":
    main()
