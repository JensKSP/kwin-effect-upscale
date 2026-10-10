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
NEEDED = ("wine", "wine64", "python3-pil", "python3-evdev")
# Where the check clicks, in the output's pixels: a place only the enlarged
# picture puts a button of the game's at, below the unscaled window's 2560 x
# 1440 at its corner. It is "No" in the question SuperTuxKart asks at its
# first start, whether it may connect to its servers, which closes on it.
CLICK: tuple[int, int] | None = (1372, 1620)
OUTPUT = (3840, 2160)
# All games' four slots, which the check switches Off for one run.
SLOTS = (
    "MethodWaylandFullScreen",
    "MethodWaylandBorderless",
    "MethodX11FullScreen",
    "MethodX11Borderless",
)
# Pictures differ where more than this share of the sampled pixels changed.
CHANGED = 0.05
# A pixel differs where its three channels differ by more than this together.
DIFFERENT = 48
# How long the game takes from its first frame to its menu.
MENU_SECONDS = 30


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


class VirtualPointer:
    """An absolute pointer, as QEMU's tablet is, made through uinput: KWin takes it as any mouse.

    It reaches a place on the output exactly, which a relative mouse under
    the session's pointer acceleration would not.
    """

    RANGE = 32767

    def __init__(self) -> None:
        """Create the device; KWin finds it as the session finds any new mouse."""
        # Installed in the machine by this check, where the check runs.
        from evdev import AbsInfo, UInput, ecodes  # type: ignore[import-not-found]  # noqa: PLC0415

        self.codes = ecodes
        axis = AbsInfo(value=0, min=0, max=self.RANGE, fuzz=0, flat=0, resolution=0)
        capabilities = {
            ecodes.EV_KEY: [ecodes.BTN_LEFT, ecodes.BTN_RIGHT],
            ecodes.EV_ABS: [(ecodes.ABS_X, axis), (ecodes.ABS_Y, axis)],
        }
        self.device = UInput(capabilities, name="upscale check pointer")

    def move(self, place: tuple[int, int]) -> None:
        """Put the pointer at @p place, in the output's pixels."""
        for code, value, size in zip(
            (self.codes.ABS_X, self.codes.ABS_Y), place, OUTPUT, strict=True
        ):
            self.device.write(self.codes.EV_ABS, code, round(value * self.RANGE / (size - 1)))
        self.device.syn()

    def click(self) -> None:
        """Press and release the left button where the pointer is."""
        for state in (1, 0):
            self.device.write(self.codes.EV_KEY, self.codes.BTN_LEFT, state)
            self.device.syn()
            time.sleep(0.1)

    def close(self) -> None:
        """Remove the device."""
        self.device.close()


def changed_share(before: Path, after: Path) -> float:
    """Say how much of two pictures of the screen differs, on a sampled grid."""
    from PIL import Image  # noqa: PLC0415 - installed in the machine by this check

    with Image.open(before) as first, Image.open(after) as second:
        one, two = first.convert("RGB"), second.convert("RGB")
        width, height = one.size
        points = [(x, y) for x in range(0, width, 16) for y in range(0, height, 16)]
        differ = sum(
            1
            for point in points
            if sum(
                abs(a - b) for a, b in zip(one.getpixel(point), two.getpixel(point), strict=True)
            )
            > DIFFERENT
        )
    return differ / len(points)


def try_input(
    pointer: VirtualPointer, environment: dict[str, str], driver: str, directory: Path
) -> dict[str, object]:
    """Click the game's button where the effect delivers it, and see whether the game answered.

    The pointer goes into the game first, where KWin engages the confinement
    Wine asks for a fullscreen game on its X11 driver; the journal says when it
    is engaged. A free pointer is mapped onto the picture, so the click goes
    where the picture shows the button, below the unscaled window. A confined
    one passes one to one in the window's own coordinates (K15, the interim
    mapping of item 29a), so the click goes where the game itself has the
    button, at its own size.
    """
    since = time.strftime("%Y-%m-%d %H:%M:%S")
    middle = (OUTPUT[0] // 2, OUTPUT[1] // 2)
    pointer.move(middle)
    time.sleep(2)
    confined = any(
        "Input observed" in line and "confinement 1" in line
        for line in pc.journal(since).splitlines()
    )
    found: dict[str, object] = {"confined": confined}
    if CLICK is None:
        return found
    target = CLICK
    if confined:
        size = [int(part) for part in pc.metrics().get("supplied", "x").split("x") if part]
        if len(size) == len(OUTPUT):
            target = (CLICK[0] * size[0] // OUTPUT[0], CLICK[1] * size[1] // OUTPUT[1])
    found["clicked at"] = list(target)
    before = directory / f"wine-supertuxkart-{driver}-question.png"
    after = directory / f"wine-supertuxkart-{driver}-answered.png"
    pointer.move(target)
    time.sleep(2)
    found["question picture"] = ps.picture(before, environment)
    pointer.click()
    time.sleep(5)
    found["answered picture"] = ps.picture(after, environment)
    missing = [picture.name for picture in (before, after) if not picture.exists()]
    if missing:
        # A picture that could not be taken says nothing about the game's input.
        found["capture missing"] = missing
    else:
        found["changed"] = round(changed_share(before, after), 3)
    found["answered"] = float(str(found.get("changed", 0))) > CHANGED
    return found


def run_driver(
    game: Path,
    environment: dict[str, str],
    driver: str,
    directory: Path,
    pointer: VirtualPointer,
) -> dict[str, object]:
    """Start the game on one driver until the effect enlarges it, then click and picture it."""
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
            # The first frame enlarged is the game's loading screen; its menu
            # follows once the game has read its data, which llvmpipe slows.
            time.sleep(MENU_SECONDS)
            seen["picture"] = ps.picture(directory / f"wine-supertuxkart-{driver}.png", environment)
            seen.update(try_input(pointer, environment, driver, directory))
            return "enlarged"
        return ""

    folder, name = shlex.quote(str(game.parent)), shlex.quote(game.name)
    command = f"cd {folder} && exec wine {name} > {shlex.quote(str(output))} 2>&1"
    try:
        found = pc.watch(["sh", "-c", command], environment, 600, enlarged)
    finally:
        pw.stop_wine()
    answered = CLICK is None or bool(seen.get("answered"))
    result: dict[str, object] = {"passed": bool(found) and answered}
    result.update(seen)
    return result


def reconfigure() -> None:
    """Have the effect read its settings again, as Apply on its settings page does."""
    pc.output(
        pc.as_user(
            *("busctl", "--user", "call", "org.kde.KWin", "/Effects"),
            *("org.kde.kwin.Effects", "reconfigureEffect", "s", "upscale"),
        )
    )


def set_slots(value: str | None) -> None:
    """Set All games' four slots to @p value, or remove them where it is None, and apply it."""
    for slot in SLOTS:
        setting = ("--delete",) if value is None else (value,)
        pc.output(pc.as_user(*pw.TAKE_ALL, "--key", slot, *setting))
    reconfigure()


def run_switched_off(game: Path, environment: dict[str, str], directory: Path) -> dict[str, object]:
    """Start the game with All games' slots Off: it is told nothing and renders at full size."""
    output = directory / "supertuxkart-off.txt"
    seen: dict[str, object] = {}

    def full_size() -> str:
        reading = pc.metrics()
        seen["metrics"] = reading
        full = "x".join(map(str, OUTPUT))
        if "supertuxkart" in reading.get("window", "").lower() and reading.get("supplied") == full:
            seen["status"] = pc.effects("supportInformation", "upscale")
            return "full size"
        return ""

    folder, name = shlex.quote(str(game.parent)), shlex.quote(game.name)
    command = f"cd {folder} && exec wine {name} > {shlex.quote(str(output))} 2>&1"
    try:
        set_slots("Off")
        found = pc.watch(["sh", "-c", command], environment, 600, full_size)
    finally:
        pw.stop_wine()
        set_slots(None)
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
    # The slots as a fresh installation has them, Auto, whatever an earlier
    # run of a check in this machine left.
    for slot in SLOTS:
        pc.output(pc.as_user(*pw.TAKE_ALL, "--key", slot, "--delete"))
    try:
        current = pc.relogin(pc.kwin())
        session = pc.Session(time.strftime("%Y-%m-%d %H:%M:%S"), current)
        session.environment = pc.session_environment()
        runs: dict[str, dict[str, object]] = {}
        result["runs"] = runs
        pointer = VirtualPointer()
        try:
            # Through the proxy first: the supported scope's route for a game
            # that Wine runs outside Steam, and the one its slot is switched
            # Off on.
            for driver in ("x11", "wayland"):
                pw.set_driver(driver)
                environment = pw.for_driver(session.environment, driver)
                runs[driver] = run_driver(game, environment, driver, directory, pointer)
            pw.set_driver("x11")
            runs["x11 switched off"] = run_switched_off(
                game, pw.for_driver(session.environment, "x11"), directory
            )
        finally:
            pointer.close()
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
            # A machine left with the package installed has not passed.
            result["passed"] = False
        Path(options.report).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(result, indent=1, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
