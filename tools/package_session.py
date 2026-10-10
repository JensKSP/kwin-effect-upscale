#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inside a package machine: an upgrade, and the session in each shipped language.

    package_session.py upgrade --from OLD --to NEW --report FILE
    package_session.py languages --package FILE --report FILE

What package_check.py does not: whether a package installed over an older one
leaves the session running the new effect and settings module after the next
login, and whether a session in German, French or Spanish shows the effect in
that language. Run as root in a machine made by tools/package-vm.py, like the
package check, whose session helpers this uses; the package is removed at the
end either way.

The language check also takes KWin's picture of the settings page and of the
display over an enlarged game in each language, and in German, the longest,
on a desktop scaled twice. They go beside the report, to be read by eye.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import package_check as pc

LIBRARIES = Path("/usr/lib")
PLUGINS = ("kwin/effects/plugins/upscale.so", "kwin/effects/configs/kwin_upscale_config.so")
CATALOGUES = Path(__file__).resolve().parent.parent / "po"
LOCALE = Path("/home") / pc.USER / ".config/plasma-localerc"
# A line the effect's status carries once it presents a window.
ALWAYS = "HDR follows KWin's color management."
# Each shipped language with the formats a person choosing it gets.
LANGUAGES = {"de": "de_DE", "fr": "fr_FR", "es": "es_ES"}
# Where a system keeps the locale definitions apart from the C library.
DEFINITIONS = {"apt-get": "locales", "dnf": "glibc-locale-source", "zypper": "glibc-i18ndata"}
# The display with every block, kept long enough to be pictured.
SHOWN = {"OsdStatistics": "true", "OsdDeveloper": "true", "OsdTimeout": "60"}
VERSION = re.compile(r"\d+\.\d+\.\d+(?:[~+][0-9A-Za-z.~+-]*)?")


def versions_in(binary: bytes) -> list[str]:
    """Find the version strings a library carries, as Qt keeps literals: in UTF-16."""
    found: list[str] = []
    for offset in (0, 1):
        text = binary[offset:].decode("utf-16-le", errors="replace")
        found += [match.group(0) for match in VERSION.finditer(text)]
    return sorted(set(found))


def translation(catalogue: str, message: str) -> str:
    """Read one message's translation out of a PO catalogue, or nothing."""
    entries = catalogue.split("\n\n")
    for entry in entries:
        lines = entry.splitlines()
        identifier = "".join(re.findall(r'^msgid "(.*)"$', "\n".join(lines), re.MULTILINE))
        if identifier == message and "msgctxt" not in entry:
            found = re.search(r'^msgstr ((?:".*"\n?)+)', entry + "\n", re.MULTILINE)
            return "".join(re.findall(r'"(.*)"', found.group(1))) if found else ""
    return ""


def speaks(status: str, language: str) -> bool:
    """Whether the effect's status carries the language's translation of its standing line."""
    catalogue = CATALOGUES / language / "kwin_effect_upscale.po"
    expected = translation(catalogue.read_text(encoding="utf-8"), ALWAYS)
    return bool(expected) and expected in status and ALWAYS not in status


def upstream(version: str) -> str:
    """Name the version as the build does: the package's, without its packaging revision."""
    return version.rsplit("-", 1)[0]


def package_version(package: str) -> str:
    """Ask the package file which version it is, as its own system would."""
    queries = {
        ".deb": ["dpkg-deb", "--field", package, "Version"],
        ".rpm": ["rpm", "--query", "--package", "--queryformat", "%{VERSION}", package],
        ".zst": ["pacman", "--query", "--file", package],
    }
    query = next((command for suffix, command in queries.items() if package.endswith(suffix)), [])
    answer = subprocess.run(query, capture_output=True, text=True, check=False).stdout.split()
    return answer[-1] if answer else ""


def installed(name: str) -> Path | None:
    """Where the installed plugin is, under whichever multiarch directory the system uses."""
    found = sorted(LIBRARIES.glob(f"**/qt6/plugins/{name}"))
    return found[0] if found else None


def identity() -> dict[str, object]:
    """Say what the session runs and what is installed, after the last login."""
    status = pc.effects("supportInformation", "upscale")
    running = next((line for line in status.splitlines() if line.startswith("build: ")), "")
    files = {name: installed(name) for name in PLUGINS}
    return {
        "running effect": running,
        **{
            f"installed {name}": versions_in(path.read_bytes()) if path else "missing"
            for name, path in files.items()
        },
    }


def install(package: str) -> None:
    """Install a package file with the system's package manager."""
    command, _remove = pc.manager()
    pc.output([*command, package], timeout=1800)


def remove() -> None:
    """Remove the package, whatever state the run left it in."""
    _install, command = pc.manager()
    pc.output(list(command), timeout=900)


def upgrade(old: str, new: str, result: dict[str, object]) -> None:
    """Install the old package, log in, upgrade to the new one, log in again."""
    current = pc.kwin()
    install(old)
    current = pc.relogin(current)
    result["after installing the old package"] = identity()
    install(new)
    # Until the next login KWin keeps the effect it loaded, and its status has
    # to name that build, not the one now installed.
    before = identity()
    result["after upgrading, before logging in again"] = before
    current = pc.relogin(current)
    after = identity()
    result["after upgrading"] = after
    # Read before they are removed: the versions the two builds name.
    kept, wanted = upstream(package_version(old)), upstream(package_version(new))
    result["new version"] = wanted
    result["passed"] = (
        bool(kept and wanted)
        and f"upscale {kept} " in str(before["running effect"])
        and all(
            wanted in found
            for key, found in after.items()
            if key.startswith("installed ") and isinstance(found, list)
        )
        and f"upscale {wanted} " in str(after["running effect"])
    )


def provide(locale: str) -> None:
    """Make the locale exist, as a system does once its language is chosen."""
    if f"{locale}.utf8" in pc.output(["locale", "-a"]).split():
        return
    if not Path("/usr/share/i18n/locales", locale).exists():
        for program, package in DEFINITIONS.items():
            if shutil.which(program):
                install(package)
    pc.output(["localedef", "-i", locale, "-f", "UTF-8", f"{locale}.UTF-8"])


def speak(code: str) -> None:
    """Set the tester's language and formats as Plasma's settings write them, or none."""
    if code:
        LOCALE.parent.mkdir(parents=True, exist_ok=True)
        LOCALE.write_text(
            f"[Formats]\nLANG={LANGUAGES[code]}.UTF-8\n\n[Translations]\nLANGUAGE={code}\n"
        )
        shutil.chown(LOCALE, pc.USER, pc.USER)
    else:
        LOCALE.unlink(missing_ok=True)


def show_everything(*, shown: bool) -> None:
    """Turn every block of the display on for the tester, or back to the defaults."""
    for key, value in SHOWN.items():
        written = [value] if shown else ["--delete"]
        command = ("kwriteconfig6", "--file", "kwinrc", "--group", "Effect-upscale", "--key", key)
        pc.output(pc.as_user(*command, *written))


def scale(factor: str, environment: dict[str, str]) -> None:
    """Scale every enabled output of the session's desktop."""
    shown = json.loads(pc.output(pc.as_user("kscreen-doctor", "--json", environment=environment)))
    for screen in shown["outputs"]:
        if screen.get("enabled"):
            change = f"output.{screen['name']}.scale.{factor}"
            pc.output(pc.as_user("kscreen-doctor", change, environment=environment))


def picture(path: Path, environment: dict[str, str]) -> str:
    """Take KWin's picture of the whole desktop, as its screenshot program does."""
    command = ("spectacle", "--background", "--nonotify", "--fullscreen", "--output", str(path))
    taken = subprocess.run(
        pc.as_user(*command, environment=environment),
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    return path.name if taken.returncode == 0 and path.exists() else taken.stderr.strip()[-200:]


def settings_picture(path: Path, environment: dict[str, str]) -> str:
    """Open the settings page in the session and picture it."""
    page = subprocess.Popen(
        pc.as_user(
            "kcmshell6", "kwin/effects/configs/kwin_upscale_config", environment=environment
        ),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        # Nothing outside the page says when it has drawn its first frame.
        time.sleep(10)
        return picture(path, environment)
    finally:
        page.terminate()
        page.wait(timeout=15)


def game_picture(path: Path, environment: dict[str, str]) -> dict[str, str]:
    """Run the race until the effect enlarges it, picture the display, read the status."""
    found = {"enlarged": "", "picture": "", "status": ""}

    def seen() -> str:
        if enlarged := pc.enlarged_game():
            # The rates need a few frames before the statistics show them.
            time.sleep(5)
            found.update(
                enlarged=enlarged,
                picture=picture(path, environment),
                status=pc.effects("supportInformation", "upscale"),
            )
        return found["enlarged"]

    pc.set_up_game()
    pc.watch(pc.RACE, environment | {"SDL_VIDEODRIVER": "wayland"}, 300, seen)
    return found


def look(name: str, directory: Path, current: str) -> dict[str, str]:
    """Picture the settings page and the display over the game in the session."""
    environment = pc.session_environment()
    pc.settle(current)
    settings = settings_picture(directory / f"{name}-settings.png", environment)
    game = game_picture(directory / f"{name}-game.png", environment)
    return {
        "settings": settings,
        "game": game["enlarged"],
        "game picture": game["picture"],
        "status while enlarged": game["status"],
    }


def pictured(seen: dict[str, str]) -> bool:
    """Say whether both pictures were taken."""
    return seen["settings"].endswith(".png") and seen["game picture"].endswith(".png")


def languages(package: str, result: dict[str, object], directory: Path) -> None:
    """Install the package and log in once per shipped language, reading the effect's status."""
    current = pc.kwin()
    install(package)
    show_everything(shown=True)
    spoken: list[bool] = []
    looks: list[dict[str, str]] = []
    cleanup: list[str] = []
    try:
        for code, locale in LANGUAGES.items():
            provide(locale)
            speak(code)
            current = pc.relogin(current)
            looks.append(look(code, directory, current))
            spoken.append(speaks(looks[-1]["status while enlarged"], code))
            result[code] = {"speaks": spoken[-1], **looks[-1]}
        speak("de")
        scale("2", pc.session_environment())
        current = pc.relogin(current)
        looks.append(look("de-scaled", directory, current))
        result["de at scale 2"] = looks[-1]
    finally:
        speak("")
        # Each step back is tried on its own and recorded, so that a failing
        # one neither skips the next nor hides what the check itself found.
        for step in (
            lambda: show_everything(shown=False),
            lambda: scale("1", pc.session_environment()),
            lambda: pc.relogin(current),
        ):
            try:
                step()
            except RuntimeError as error:
                cleanup.append(str(error))
        if cleanup:
            result["cleanup"] = cleanup
    result["passed"] = (
        not cleanup and len(spoken) == len(LANGUAGES) and all(spoken) and all(map(pictured, looks))
    )


def main(argv: list[str] | None = None) -> int:
    """Run one of the two checks and write its report."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)
    upgrading = commands.add_parser("upgrade")
    upgrading.add_argument("--from", dest="old", required=True)
    upgrading.add_argument("--to", dest="new", required=True)
    upgrading.add_argument("--report", required=True)
    speaking = commands.add_parser("languages")
    speaking.add_argument("--package", required=True)
    speaking.add_argument("--report", required=True)
    options = parser.parse_args(argv)
    # Filled as the check goes, so that a failure still reports what was found.
    result: dict[str, object] = {"passed": False}
    try:
        if options.command == "upgrade":
            upgrade(options.old, options.new, result)
        else:
            languages(options.package, result, Path(options.report).parent)
    except RuntimeError as error:
        result["error"] = str(error)
    finally:
        try:
            remove()
        except RuntimeError as error:
            result["removal"] = str(error)
            # A machine left with the package installed has not passed.
            result["passed"] = False
        Path(options.report).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(result, indent=1, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
