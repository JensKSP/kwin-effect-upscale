#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: a Windows game on Wine's Wayland driver.

    package_wine.py --package FILE --report FILE

Wine's Wayland driver draws an OpenGL window into one subsurface covering it,
in a buffer whose height it rounds up to a multiple of 128 and shows the
window's size of through a viewport. tools/wine-opengl-probe.c, built here
with MinGW, draws the left half of its borderless window over the whole screen
red and the right half blue, under the system's Wine with the prefix's
graphics driver set to wayland. The effect is told to take every application,
as Wine's programs have no entry of their own, and what is checked is that it
enlarges the probe from a smaller picture to the whole output, and that KWin's
picture of the screen splits red from blue at its middle. Run as root in a
machine made by tools/package-vm.py, like the package check, whose session
helpers this uses; Debian's and Ubuntu's package names only. The package is
removed at the end either way.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import package_check as pc
import package_session as ps

SOURCE = Path(__file__).with_name("wine-opengl-probe.c")
# Wine, the compiler for the probe, and the reader of the picture.
NEEDED = ("wine", "wine64", "gcc-mingw-w64-x86-64", "python3-pil")
PREFIX = Path("/home") / pc.USER / ".wine-upscale"
# No Mono or Gecko installer asking at the prefix's first start, nor Wine's log.
WINE = {"WINEPREFIX": str(PREFIX), "WINEDEBUG": "-all", "WINEDLLOVERRIDES": "mscoree,mshtml="}
# A channel brighter than this is lit, a darker one is not.
BRIGHT = 200
TAKE_ALL = ("kwriteconfig6", "--file", "kwinrc", "--group", "Effect-upscale")
TAKE_ALL_KEY = ("--key", "UnlistedApplications")


def wayland_only(environment: dict[str, str]) -> dict[str, str]:
    """Give a Wine program the session's environment, without the X11 display to fall back on."""
    return {key: value for key, value in environment.items() if key != "DISPLAY"} | WINE


def build_probe(directory: Path) -> Path:
    """Build the probe into a directory the tester owns."""
    probe = directory / "probe.exe"
    compiler = ("x86_64-w64-mingw32-gcc", "-O2", "-o", str(probe), str(SOURCE))
    pc.output([*compiler, "-lopengl32", "-lgdi32"], timeout=300)
    shutil.chown(directory, user=pc.USER)
    return probe


def wine(*command: str, timeout: float = 900) -> None:
    """Run a Wine command as the tester, without pipes, and raise if it failed.

    Wine leaves its server and its desktop running after the command, holding
    whatever it was started with: a pipe to read its output from is never
    closed, and the reading never ends (wineboot, 2026-10-07).
    """
    done = subprocess.run(
        pc.as_user(*command, environment=wayland_only({})),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=timeout,
        check=False,
    )
    if done.returncode:
        message = f"{command[0]} failed with exit {done.returncode}"
        raise RuntimeError(message)


def make_prefix() -> None:
    """Make the tester's prefix, set its graphics driver to Wine's Wayland driver, and stop it.

    The server is ended rather than asked to shut the prefix down: Wine's own
    shutdown left its services and desktop running, and restarting, in a
    session whose screen had locked (2026-10-07). The server writes the
    registry as it ends.
    """
    wine("wineboot", "--init")
    drivers = ("reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Graphics", "/d", "wayland")
    wine("wine", *drivers, "/f")
    wine("wineserver", "-k")
    wine("wineserver", "-w")


def split_at_middle(picture: Path) -> str:
    """Say where KWin's picture is red and where blue along the middle row, if it splits."""
    from PIL import Image  # noqa: PLC0415 - installed in the machine by this check

    with Image.open(picture) as image:
        rgb = image.convert("RGB")
        width, height = rgb.size
        row = height // 2

        def colour(x: int) -> str:
            red, _, blue = rgb.getpixel((x, row))
            return "red" if red > BRIGHT > blue else "blue" if blue > BRIGHT > red else "other"

        marks = {x: colour(x) for x in (width // 4, width // 2 - 8, width // 2 + 8, 3 * width // 4)}
    found = ", ".join(f"{x}: {seen}" for x, seen in marks.items())
    expected = ["red", "red", "blue", "blue"]
    return f"split at the middle of {width} ({found})" if list(marks.values()) == expected else ""


def probe_run(probe: Path, session: pc.Session, result: dict[str, object], directory: Path) -> None:
    """Run the probe until the effect enlarges it, and picture the screen beside the report."""
    output = probe.with_name("probe.txt")
    picture = directory / "wine-probe.png"
    environment = wayland_only(session.environment)
    # The desktop before the game, as every program in the session was told
    # under All applications, to be read by eye.
    result["desktop picture"] = ps.picture(directory / "wine-desktop.png", environment)
    seen: dict[str, object] = {}

    def enlarged() -> str:
        reading = pc.metrics()
        seen["metrics"] = reading
        seen["status"] = pc.effects("supportInformation", "upscale")
        if (
            reading.get("selected") == "1"
            and reading.get("scaling") == "1"
            and reading.get("windowsystem") == "wayland"
            and reading.get("supplied", "") not in ("", reading.get("destination"))
        ):
            seen["picture"] = ps.picture(picture, environment)
            return "enlarged"
        return ""

    command = f"exec wine {probe} 120 > {output} 2>&1"
    found = pc.watch(["sh", "-c", command], environment, 240, enlarged)
    result["probe said"] = (
        output.read_text(errors="replace").splitlines()[-20:] if output.exists() else []
    )
    result.update(seen)
    taken = str(seen.get("picture", "")).endswith(".png")
    split = split_at_middle(picture) if found and taken else ""
    result["picture splits"] = split
    result["passed"] = bool(found and split)


def wine_run(package: str, result: dict[str, object], directory: Path) -> None:
    """Provide Wine and the probe, install the package, and run the probe in a new session."""
    install, _ = pc.manager()
    if install[0] != "apt-get":
        message = "the packages this check needs are named for Debian and Ubuntu only"
        raise RuntimeError(message)
    pc.output([*install, *NEEDED], timeout=3600)
    result["wine"] = pc.output(["wine", "--version"]).strip()
    probe = build_probe(Path(tempfile.mkdtemp(prefix="wine-probe-")))
    make_prefix()
    pc.output([*install, package], timeout=1800)
    pc.output(pc.as_user(*TAKE_ALL, *TAKE_ALL_KEY, "true"))
    try:
        current = pc.relogin(pc.kwin())
        session = pc.Session(time.strftime("%Y-%m-%d %H:%M:%S"), current)
        session.environment = pc.session_environment()
        probe_run(probe, session, result, directory)
    finally:
        pc.output(pc.as_user(*TAKE_ALL, *TAKE_ALL_KEY, "--delete"))


def main(argv: list[str] | None = None) -> int:
    """Run the probe and write its report."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--package", required=True)
    parser.add_argument("--report", required=True)
    options = parser.parse_args(argv)
    result: dict[str, object] = {"passed": False}
    try:
        wine_run(options.package, result, Path(options.report).parent)
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        _, remove = pc.manager()
        try:
            pc.output(list(remove), timeout=600)
        except RuntimeError as error:
            result["removal"] = str(error)
        Path(options.report).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(result, indent=1, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
