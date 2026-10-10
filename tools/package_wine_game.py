#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: a real Windows game under Wine, launched as a player launches it.

    package_wine_game.py --package FILE --report FILE

SuperTuxKart's own Windows release, from its project's GitHub releases and
checked against the digest GitHub publishes for it, is unpacked into the
tester's home and started with `wine supertuxkart.exe` from its folder, with
no option and nothing in its environment but Wine's prefix: once on Wine's
X11 driver, through the session proxy, and once on its Wayland driver. Its
settings are a player's at a 4K screen: fullscreen at 3840 x 2160, OpenGL. The
effect's All games is switched on, as the game has no entry for its Windows
build and everything Wine runs is a game to it. A run passes where the effect
enlarges the game's window from a smaller picture to the whole output on the
window system its driver speaks. Run as root in a machine made by
tools/package-vm.py, like the Wine check, whose prefix and session helpers this
uses; Debian's and Ubuntu's package names only. The package is removed at the
end either way.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

import package_check as pc
import package_session as ps
import package_wine as pw

RELEASE = "https://github.com/supertuxkart/stk-code/releases/download/1.5/SuperTuxKart-1.5-win.zip"
# The digest GitHub publishes for that asset.
DIGEST = "9df7e2d67e8562127a3bb633030f1bd4ee77fa9e7f0ae110897473025af8acdc"
GAME = Path("/home") / pc.USER / "supertuxkart-windows"
# Where SuperTuxKart for Windows keeps its settings: %APPDATA% in the prefix.
SETTINGS = (
    pw.PREFIX / "drive_c/users" / pc.USER / "AppData/Roaming/supertuxkart/config-0.10/config.xml"
)
NEEDED = ("wine", "wine64", "python3-pil")


def fetch(directory: Path) -> Path:
    """Download the release, and refuse it unless it is the file GitHub's digest names."""
    archive = directory / Path(RELEASE).name
    # A fixed https URL: no other scheme is ever opened.
    answer = urllib.request.urlopen(RELEASE, timeout=600)  # nosec B310
    with answer, archive.open("wb") as file:
        shutil.copyfileobj(answer, file, 1 << 20)
    digest = hashlib.sha256()
    with archive.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    if digest.hexdigest() != DIGEST:
        message = f"{archive.name} has digest {digest.hexdigest()}, not the published one"
        raise RuntimeError(message)
    return archive


def executable(root: Path) -> Path:
    """Name the game's x86-64 executable among the builds the release unpacked.

    SuperTuxKart 1.5 carries one per architecture, in build-x86_64,
    build-i686, build-aarch64 and build-armv7; Wine here runs x86-64, and a
    release without that build is refused rather than another one started.
    """
    found = sorted(path for path in root.rglob("supertuxkart.exe") if x86_64(path))
    if not found:
        message = f"no x86-64 supertuxkart.exe under {root}"
        raise RuntimeError(message)
    return found[0]


def x86_64(path: Path) -> bool:
    """Whether a path names a build for x86-64, by a directory named for it."""
    return any(
        re.search(r"(^|[-_])(x86[-_]64|x64|amd64|win64)$", part.lower()) for part in path.parts
    )


def unpack(archive: Path) -> Path:
    """Unpack the release into the tester's home, and name the game's executable."""
    shutil.rmtree(GAME, ignore_errors=True)
    with zipfile.ZipFile(archive) as release:
        release.extractall(GAME)
    pc.output(["chown", "-R", f"{pc.USER}:{pc.USER}", str(GAME)])
    return executable(GAME)


def set_up_game() -> None:
    """Give the game the settings a player at a 4K screen has, where Wine's Windows keeps them."""
    pc.output(pc.as_user("mkdir", "-p", str(SETTINGS.parent)))
    pc.output(pc.as_user("sh", "-c", f"cat > {shlex.quote(str(SETTINGS))}"), input_text=pc.GAME)


def run_driver(
    game: Path, environment: dict[str, str], driver: str, directory: Path
) -> dict[str, object]:
    """Start the game on one driver until the effect enlarges it, and picture the screen."""
    output = directory / f"supertuxkart-{driver}.txt"
    seen: dict[str, object] = {}

    def enlarged() -> str:
        reading = pc.metrics()
        seen["metrics"] = reading
        if (
            "supertuxkart" in reading.get("window", "").lower()
            and reading.get("selected") == "1"
            and reading.get("scaling") == "1"
            and reading.get("windowsystem") == driver
            and reading.get("supplied", "") not in ("", reading.get("destination"))
        ):
            seen["status"] = pc.effects("supportInformation", "upscale")
            seen["picture"] = ps.picture(directory / f"wine-supertuxkart-{driver}.png", environment)
            return "enlarged"
        return ""

    folder, name = shlex.quote(str(game.parent)), shlex.quote(game.name)
    command = f"cd {folder} && exec wine {name} > {shlex.quote(str(output))} 2>&1"
    try:
        found = pc.watch(["sh", "-c", command], environment, 600, enlarged)
    finally:
        pw.stop_wine()
    result: dict[str, object] = {"passed": bool(found)}
    result.update(seen)
    return result


def game_run(package: str, result: dict[str, object], directory: Path) -> None:
    """Provide Wine and the game, install the package, and start the game on each driver."""
    install, _ = pc.manager()
    if install[0] != "apt-get":
        message = "the packages this check needs are named for Debian and Ubuntu only"
        raise RuntimeError(message)
    pc.output([*install, *NEEDED], timeout=3600)
    result["wine"] = pc.output(["wine", "--version"]).strip()
    # Beside the game rather than in /tmp, which may be memory and too small.
    with tempfile.TemporaryDirectory(prefix="supertuxkart-", dir=GAME.parent) as download:
        game = unpack(fetch(Path(download)))
    result["game"] = str(game.relative_to(GAME))
    pw.make_prefix("wine")
    set_up_game()
    pc.output([*install, package], timeout=1800)
    pc.output(pc.as_user(*pw.TAKE_ALL, *pw.TAKE_ALL_KEY, "true"))
    try:
        current = pc.relogin(pc.kwin())
        session = pc.Session(time.strftime("%Y-%m-%d %H:%M:%S"), current)
        session.environment = pc.session_environment()
        runs: dict[str, dict[str, object]] = {}
        result["runs"] = runs
        # Through the proxy first: the supported scope's route for a game
        # that Wine runs outside Steam.
        for driver in ("x11", "wayland"):
            pw.set_driver(driver)
            environment = pw.for_driver(session.environment, driver)
            runs[driver] = run_driver(game, environment, driver, directory)
        result["passed"] = all(run["passed"] for run in runs.values())
    finally:
        pc.output(pc.as_user(*pw.TAKE_ALL, *pw.TAKE_ALL_KEY, "--delete"))


def main(argv: list[str] | None = None) -> int:
    """Run the game and write its report."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--package", required=True)
    parser.add_argument("--report", required=True)
    options = parser.parse_args(argv)
    result: dict[str, object] = {"passed": False}
    try:
        game_run(options.package, result, Path(options.report).parent)
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
