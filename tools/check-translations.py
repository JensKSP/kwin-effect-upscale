#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail when a shipped translation catalogue is incomplete or stale.

The template is extracted from the sources as Messages.sh extracts it for KDE's
translation scripts, with the same keywords, and every catalogue under
po/<language>/ is merged against it in a scratch copy. A catalogue passes when
every message of the template has a translation that is neither missing nor
fuzzy, it carries none the template no longer has, and msgfmt accepts it with
its format checks, which compare the %1 placeholders. An incomplete catalogue
would otherwise fall back to English in the middle of a sentence.

With --update, each catalogue is merged with the current template instead:
a new string arrives untranslated, a changed one fuzzy, a removed one is
dropped, and the check then names what is left to translate.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DOMAIN = "kwin_effect_upscale"
# What KDE's scripts pass as $XGETTEXT, less the copyright and bug address,
# which only label the template.
KEYWORDS = [
    "--from-code=UTF-8",
    "-C",
    "--kde",
    "-ci18n",
    *(
        f"-k{name}:1"
        for name in ("i18n", "ki18n", "xi18n", "kxi18n", "I18N_NOOP", "tr2i18n", "tr2xi18n")
    ),
    *(
        f"-k{name}:1c,2"
        for name in ("i18nc", "ki18nc", "xi18nc", "kxi18nc", "I18NC_NOOP", "I18N_NOOP2")
    ),
    *(f"-k{name}:1,2" for name in ("i18np", "ki18np", "xi18np", "kxi18np")),
    *(f"-k{name}:1c,2,3" for name in ("i18ncp", "ki18ncp", "xi18ncp", "kxi18ncp")),
]
STATISTICS = re.compile(
    r"(\d+) translated messages?"
    r"(?:, (\d+) fuzzy translations?)?(?:, (\d+) untranslated messages?)?\."
)


# gettext reports in the user's language, and its statistics are read here.
ENVIRONMENT = {**os.environ, "LC_ALL": "C.UTF-8", "LANGUAGE": ""}


def run(*command: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    """Run one gettext tool and keep what it says, in English."""
    return subprocess.run(
        command, cwd=cwd, env=ENVIRONMENT, capture_output=True, text=True, check=False
    )


def sources(root: Path) -> list[str]:
    """List the C++ files Messages.sh hands to xgettext, in a stable order."""
    return sorted(
        str(path.relative_to(root))
        for path in (root / "src").rglob("*")
        if path.suffix in {".cpp", ".h"}
    )


def template(root: Path, target: Path) -> None:
    """Extract every translatable string of the sources into target."""
    result = run(
        "xgettext",
        *KEYWORDS,
        "--package-name=kwin-effect-upscale",
        "--msgid-bugs-address=https://github.com/JensKSP/kwin-effect-upscale/issues",
        "-o",
        str(target),
        *sources(root),
        cwd=root,
    )
    if result.returncode != 0:
        message = f"xgettext failed:\n{result.stderr}"
        raise SystemExit(message)


def problems(catalogue: Path, pot: Path, scratch: Path) -> list[str]:
    """Name what keeps one catalogue from being complete for this template."""
    found: list[str] = []
    checked = run("msgfmt", "--check", "-o", "/dev/null", str(catalogue))
    if checked.returncode != 0:
        found.append(checked.stderr.strip())
    # msgfmt leaves an obsolete entry out of the compiled catalogue, while the
    # merge below would revive it for a string the sources have again and so
    # count it as translated: one is a translation the user never gets.
    shipped = run("msgattrib", "--only-obsolete", str(catalogue)).stdout
    shipped_obsolete = len(re.findall(r"^#~ msgid ", shipped, flags=re.MULTILINE))
    if shipped_obsolete:
        found.append(f"{shipped_obsolete} obsolete entries, which msgfmt leaves out")
    merged = scratch / f"{catalogue.parent.name}.po"
    result = run(
        "msgmerge", "--quiet", "--no-fuzzy-matching", "-o", str(merged), str(catalogue), str(pot)
    )
    if result.returncode != 0:
        return [*found, result.stderr.strip()]
    obsolete = run("msgattrib", "--only-obsolete", str(merged)).stdout
    count = len(re.findall(r"^#~ msgid ", obsolete, flags=re.MULTILINE))
    if count:
        found.append(f"{count} translations of strings the sources no longer have")
    statistics = run("msgfmt", "--statistics", "-o", "/dev/null", str(merged)).stderr
    match = STATISTICS.search(statistics)
    if not match:
        return [*found, f"unreadable statistics: {statistics.strip()}"]
    fuzzy, untranslated = int(match.group(2) or 0), int(match.group(3) or 0)
    if fuzzy or untranslated:
        missing = run("msgattrib", "--untranslated", "--no-obsolete", str(merged)).stdout
        missing += run("msgattrib", "--only-fuzzy", "--no-obsolete", str(merged)).stdout
        ids = sorted(set(re.findall(r'^msgid "(.+)"$', missing, flags=re.MULTILINE)))
        found.append(f"{untranslated} untranslated and {fuzzy} fuzzy: " + "; ".join(ids[:10]))
    return found


def update(catalogue: Path, pot: Path) -> None:
    """Merge one catalogue with the template in place, dropping what is gone.

    Without source locations, which would change the catalogue whenever code moves.
    """
    merged = run(
        "msgmerge", "--quiet", "--previous", "--no-location", "-o", "-", str(catalogue), str(pot)
    )
    if merged.returncode != 0:
        raise SystemExit(merged.stderr)
    kept = subprocess.run(
        ["msgattrib", "--no-obsolete", "-o", str(catalogue)],
        input=merged.stdout,
        env=ENVIRONMENT,
        text=True,
        check=False,
    )
    if kept.returncode != 0:
        message = f"msgattrib failed for {catalogue}"
        raise SystemExit(message)


def main() -> int:
    """Check, or update and check, every catalogue under po/."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument(
        "--update", action="store_true", help="merge every catalogue with the current template"
    )
    arguments = parser.parse_args()
    for tool in ("xgettext", "msgmerge", "msgfmt", "msgattrib"):
        if not shutil.which(tool):
            print(f"{tool} is missing; install gettext", file=sys.stderr)
            return 2
    catalogues = sorted((arguments.root / "po").glob(f"*/{DOMAIN}.po"))
    with tempfile.TemporaryDirectory() as directory:
        scratch = Path(directory)
        pot = scratch / f"{DOMAIN}.pot"
        template(arguments.root, pot)
        failed = False
        for catalogue in catalogues:
            if arguments.update:
                update(catalogue, pot)
            for problem in problems(catalogue, pot, scratch):
                print(f"{catalogue.relative_to(arguments.root)}: {problem}", file=sys.stderr)
                failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
