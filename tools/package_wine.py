#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: Windows graphics APIs under Wine, on both of its display drivers.

    package_wine.py --package FILE --report FILE

tools/wine-probe/, built here with MinGW-w64, is a Windows program that draws
as a game does, through OpenGL, Direct3D 9, 11 or 12, or Vulkan, borderless (a
popup window over its whole screen) or exclusive (the API's own fullscreen, or
for OpenGL and Vulkan a display mode set first): the left half red, the right
half blue. It runs under the system's Wine once for each API and mode on
Wine's Wayland driver, and once for each through the X11 session proxy on
Wine's X11 driver, with a new Wine server for every run, so that each is told
its screen afresh. The effect's All games is switched on, as Wine's programs
have no entry of their own and everything Wine runs is a game to it. A run
passes where the effect enlarges the probe from a smaller picture to the
whole output on the window system its driver speaks, and KWin's picture of the
screen splits red from blue at its middle. Wine draws Direct3D through its own
translation and Vulkan on the machine's lavapipe, as llvmpipe draws the rest,
so what is checked is what the effect does with each, not how a game would run
on a graphics card. Run as root in a machine made by tools/package-vm.py, like
the package check, whose session helpers this uses; Debian's and Ubuntu's
package names only. The package is removed at the end either way.
"""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import package_check as pc
import package_session as ps

SOURCES = sorted(Path(__file__).with_name("wine-probe").glob("*.cpp"))
# Wine, the compiler for the probe and Vulkan's headers for it, the Vulkan
# driver Wine's Vulkan and Direct3D 12 draw on, and the reader of the picture.
NEEDED = (
    "wine",
    "wine64",
    "g++-mingw-w64-x86-64",
    "libvulkan-dev",
    "mesa-vulkan-drivers",
    "dxvk",
    "dxvk-wine64",
    "python3-pil",
)
APIS = ("opengl", "d3d9", "d3d11", "d3d12", "vulkan")
# Who translates the probe's Direct3D: Wine itself, with WineD3D and vkd3d,
# for every API, or DXVK, as Proton does, for Direct3D 9 and 11. Each has a
# prefix of its own, so that neither's libraries stand in for the other's.
TRANSLATIONS = {"wine": APIS, "dxvk": ("d3d9", "d3d11")}
MODES = ("borderless", "exclusive")
# Wine's display drivers, each named as the effect names the window system
# that driver speaks.
DRIVERS = ("wayland", "x11")
PREFIX = Path("/home") / pc.USER / ".wine-upscale"
PREFIXES = {"wine": PREFIX, "dxvk": Path("/home") / pc.USER / ".wine-upscale-dxvk"}
# No Mono or Gecko installer asking at the prefix's first start, nor Wine's log.
WINE = {"WINEDEBUG": "-all", "WINEDLLOVERRIDES": "mscoree,mshtml="}
# A channel brighter than this is lit, a darker one is not.
BRIGHT = 200
TAKE_ALL = ("kwriteconfig6", "--file", "kwinrc", "--group", "Effect-upscale")
TAKE_ALL_KEY = ("--key", "UnlistedApplications")


def prefixed(translation: str) -> dict[str, str]:
    """Wine's settings for the prefix of @p translation."""
    return WINE | {"WINEPREFIX": str(PREFIXES[translation])}


def wayland_only(environment: dict[str, str], translation: str = "wine") -> dict[str, str]:
    """Give a Wine program the session's environment, without the X11 display to fall back on."""
    without = {key: value for key, value in environment.items() if key != "DISPLAY"}
    return without | prefixed(translation)


def for_driver(
    environment: dict[str, str], driver: str, translation: str = "wine"
) -> dict[str, str]:
    """Give a Wine program the session's environment as the driver it runs on needs it."""
    if driver == "wayland":
        return wayland_only(environment, translation)
    return environment | prefixed(translation)


def build_probe(directory: Path) -> Path:
    """Build the probe into a directory the tester owns.

    Vulkan's headers are the system's, given to the compiler alone: the rest of
    /usr/include is Linux's and would stand in for Windows' own.
    """
    probe = directory / "probe.exe"
    headers = directory / "include"
    headers.mkdir()
    for name in ("vulkan", "vk_video"):
        (headers / name).symlink_to(Path("/usr/include") / name)
    compiler = ("x86_64-w64-mingw32-g++", "-std=c++17", "-O2", "-static", "-isystem", str(headers))
    libraries = ("-lopengl32", "-lgdi32", "-ld3d9", "-ld3d11", "-ldxgi")
    pc.output([*compiler, "-o", str(probe), *map(str, SOURCES), *libraries], timeout=600)
    shutil.chown(directory, user=pc.USER)
    return probe


def wine(
    *command: str, translation: str = "wine", timeout: float = 900, required: bool = True
) -> None:
    """Run a Wine command as the tester, without pipes, and raise if it failed and was required.

    Wine leaves its server and its desktop running after the command, holding
    whatever it was started with: a pipe to read its output from is never
    closed, and the reading never ends (wineboot, 2026-10-07).
    """
    done = subprocess.run(
        pc.as_user(*command, environment=wayland_only({}, translation)),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=timeout,
        check=False,
    )
    if required and done.returncode:
        message = f"{command[0]} failed with exit {done.returncode}"
        raise RuntimeError(message)


def stop_wine(translation: str = "wine") -> None:
    """End the prefix's server and everything it runs, and wait until it has.

    The server is ended rather than asked to shut the prefix down: Wine's own
    shutdown left its services and desktop running, and restarting, in a
    session whose screen had locked (2026-10-07). Waiting for a server nobody
    ended never returns while Wine's desktop runs. The server writes the
    registry as it ends.
    """
    # Neither has anything to do where the server has ended on its own.
    wine("wineserver", "-k", translation=translation, timeout=60, required=False)
    wine("wineserver", "-w", translation=translation, timeout=60, required=False)


def make_prefix(translation: str) -> None:
    """Make the tester's prefix for @p translation, with DXVK in it where that is DXVK, and stop it.

    Debian's dxvk-setup links DXVK's libraries into the prefix and overrides
    Wine's own with them, Direct3D 9 to 11 and DXGI.
    """
    wine("wineboot", "--init", translation=translation)
    stop_wine(translation)
    if translation == "dxvk":
        wine("dxvk-setup", "install", "--yes", "--stable", translation=translation, timeout=300)
        stop_wine(translation)


def set_driver(driver: str, translation: str = "wine") -> None:
    """Set the prefix's graphics driver, which Wine reads when its server starts."""
    drivers = ("reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Graphics", "/d", driver)
    wine("wine", *drivers, "/f", translation=translation)
    stop_wine(translation)


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


def run_case(
    probe: Path, environment: dict[str, str], name: str, directory: Path
) -> dict[str, object]:
    """Run the probe for one driver, translation, API and mode until it is enlarged; picture it."""
    driver, translation, api, mode = name.split()
    output = probe.with_name(f"{driver}-{translation}-{api}-{mode}.txt")
    picture = directory / f"wine-{driver}-{translation}-{api}-{mode}.png"
    seen: dict[str, object] = {}

    def enlarged() -> str:
        reading = pc.metrics()
        seen["metrics"] = reading
        if (
            "probe" in reading.get("window", "")
            and reading.get("selected") == "1"
            and reading.get("scaling") == "1"
            and reading.get("windowsystem") == driver
            and reading.get("supplied", "") not in ("", reading.get("destination"))
        ):
            seen["status"] = pc.effects("supportInformation", "upscale")
            seen["picture"] = ps.picture(picture, environment)
            return "enlarged"
        return ""

    command = (
        f"exec wine {shlex.quote(str(probe))} {api} {mode} 120 > {shlex.quote(str(output))} 2>&1"
    )
    # Its server ends with the case whatever happens in it, or the next case
    # would be shown the screen this one's server read.
    try:
        found = pc.watch(["sh", "-c", command], environment, 150, enlarged)
        # The probe's own words, without the graphics stack's warnings about a
        # machine that has no graphics card.
        said = output.read_text(errors="replace").splitlines() if output.exists() else []
        case: dict[str, object] = {
            "probe said": [
                line for line in said if line.startswith(("screen", "client", "unavailable"))
            ][-4:]
        }
        case.update(seen)
        taken = str(seen.get("picture", "")).endswith(".png")
        case["picture splits"] = split_at_middle(picture) if found and taken else ""
        case["passed"] = bool(found and case["picture splits"])
    finally:
        stop_wine(translation)
    return case


def probe_runs(
    probe: Path, session: pc.Session, result: dict[str, object], directory: Path
) -> None:
    """Run every driver, translation, API and mode in turn; pass where every one was enlarged."""
    # The desktop before the games, under All games, which tells the desktop's
    # own programs nothing: its panel spans the screen. To be read by eye.
    result["desktop picture"] = ps.picture(
        directory / "wine-desktop.png", wayland_only(session.environment)
    )
    cases: dict[str, dict[str, object]] = {}
    result["cases"] = cases
    for driver in DRIVERS:
        for translation, apis in TRANSLATIONS.items():
            set_driver(driver, translation)
            environment = for_driver(session.environment, driver, translation)
            for api in apis:
                for mode in MODES:
                    name = f"{driver} {translation} {api} {mode}"
                    cases[name] = run_case(probe, environment, name, directory)
    result["passed"] = all(case["passed"] for case in cases.values())


def wine_run(package: str, result: dict[str, object], directory: Path) -> None:
    """Provide Wine and the probe, install the package, and run the probe in a new session."""
    install, _ = pc.manager()
    if install[0] != "apt-get":
        message = "the packages this check needs are named for Debian and Ubuntu only"
        raise RuntimeError(message)
    pc.output([*install, *NEEDED], timeout=3600)
    result["wine"] = pc.output(["wine", "--version"]).strip()
    probe = build_probe(Path(tempfile.mkdtemp(prefix="wine-probe-")))
    for translation in TRANSLATIONS:
        make_prefix(translation)
    pc.output([*install, package], timeout=1800)
    pc.output(pc.as_user(*TAKE_ALL, *TAKE_ALL_KEY, "true"))
    try:
        current = pc.relogin(pc.kwin())
        session = pc.Session(time.strftime("%Y-%m-%d %H:%M:%S"), current)
        session.environment = pc.session_environment()
        probe_runs(probe, session, result, directory)
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
