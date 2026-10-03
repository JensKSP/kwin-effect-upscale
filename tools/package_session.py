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
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import time
from pathlib import Path

import package_check as pc

LIBRARIES = Path("/usr/lib")
PLUGINS = ("kwin/effects/plugins/upscale.so", "kwin/effects/configs/kwin_upscale_config.so")
CATALOGUES = Path(__file__).resolve().parent.parent / "po"
LOCALE = Path("/home") / pc.USER / ".config/plasma-localerc"
# A line the effect's status always carries, whatever it sees.
ALWAYS = "HDR follows KWin's color management."
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


def installed(name: str) -> Path | None:
    """Where the installed plugin is, under whichever multiarch directory the system uses."""
    found = sorted(LIBRARIES.glob(f"**/qt6/plugins/{name}"))
    return found[0] if found else None


def identity() -> dict[str, object]:
    """What the session runs and what is installed, after the last login."""
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
    pc.output(command, timeout=900)


def upgrade(old: str, new: str) -> dict[str, object]:
    """Install the old package, log in, upgrade to the new one, log in again."""
    result: dict[str, object] = {}
    current = pc.kwin()
    install(old)
    current = pc.relogin(current)
    result["after installing the old package"] = identity()
    install(new)
    current = pc.relogin(current)
    after = identity()
    result["after upgrading"] = after
    wanted = VERSION.search(Path(new).name)
    result["new version"] = wanted.group(0) if wanted else ""
    result["passed"] = bool(wanted) and all(
        any(version.startswith(wanted.group(0)) for version in found)
        for key, found in after.items()
        if key.startswith("installed ") and isinstance(found, list)
    ) and wanted.group(0) in str(after["running effect"])
    return result


def speak(code: str) -> None:
    """Set the tester's session language, or the system's when the code is empty."""
    if code:
        LOCALE.parent.mkdir(parents=True, exist_ok=True)
        LOCALE.write_text(f"[Translations]\nLANGUAGE={code}\n")
        shutil.chown(LOCALE, pc.USER, pc.USER)
    else:
        LOCALE.unlink(missing_ok=True)


def languages(package: str) -> dict[str, object]:
    """Install the package and log in once per shipped language, reading the effect's status."""
    result: dict[str, object] = {}
    current = pc.kwin()
    install(package)
    try:
        for code in ("de", "fr", "es"):
            speak(code)
            current = pc.relogin(current)
            time.sleep(5)
            status = pc.effects("supportInformation", "upscale")
            result[code] = {"speaks": speaks(status, code), "status": status.splitlines()[:8]}
    finally:
        speak("")
        pc.relogin(current)
    result["passed"] = all(
        isinstance(entry, dict) and entry["speaks"] for key, entry in result.items() if key != "passed"
    )
    return result


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
    result: dict[str, object] = {"passed": False}
    try:
        if options.command == "upgrade":
            result = upgrade(options.old, options.new)
        else:
            result = languages(options.package)
    finally:
        try:
            remove()
        except RuntimeError as error:
            result["removal"] = str(error)
        Path(options.report).write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps(result, indent=1, ensure_ascii=False))
    return 0 if result.get("passed") else 1


if __name__ == "__main__":
    sys.exit(main())
