#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Put together the handbook's site, for GitHub Pages to build.

    build-site.py <directory>

The site holds what people read: the README as its front page and the permanent
documents under doc/. It leaves out what agents read, doc/AGENTS.md and
doc/agents/, as Jens decided on 2026-09-29 (item 86a of the open list). A link
from a page to anything the site does not hold, an agent document or a file of
the repository, goes to that file on GitHub instead, so that no link on the
site leads nowhere. Jekyll, run by GitHub's own action, turns the pages into
HTML and keeps their links between each other working.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
REPOSITORY = "https://github.com/JensKSP/kwin-effect-upscale/blob/master/"
# The front page, and the documents beside it, by their paths in the repository.
FRONT = "README.md"
PAGES = (
    "doc/upscaling.md",
    "doc/checks.md",
    "doc/conventions.md",
    "doc/pull-requests.md",
    "doc/releases.md",
    "doc/third-party-notices.md",
)
LINK = re.compile(r"(\]\()([^)\s]+)(\))")
CONFIG = """title: KWin Upscale
description: FSR upscaling for fullscreen games in KDE Plasma
markdown: kramdown
kramdown:
  input: GFM
"""


def rewrite(text: str, page: str, held: set[str]) -> str:
    """Point the links of one page at the site where it holds the target, else at GitHub."""

    def target(match: re.Match[str]) -> str:
        link = match[2]
        if re.match(r"^[a-z][a-z0-9+.-]*:", link) or link.startswith("#"):
            return match[0]
        path, _, anchor = link.partition("#")
        resolved = str(PurePosixPath(page).parent / path)
        parts: list[str] = []
        for part in PurePosixPath(resolved).parts:
            if part == "..":
                if parts:
                    parts.pop()
            elif part != ".":
                parts.append(part)
        resolved = "/".join(parts)
        if resolved in held:
            return match[0]
        where = REPOSITORY + resolved + (f"#{anchor}" if anchor else "")
        return f"{match[1]}{where}{match[3]}"

    return LINK.sub(target, text)


def build(destination: Path, root: Path = ROOT) -> list[Path]:
    """Write the site's source into a directory, and name what it wrote."""
    held = {FRONT, *PAGES}
    written = []
    for page in (FRONT, *PAGES):
        name = "index.md" if page == FRONT else page
        text = rewrite((root / page).read_text(), page, held)
        if page == FRONT:
            # The front page lies at the site's root under its own name, so its
            # links to the pages keep their paths.
            text = text.replace("](README.md", "](index.md")
        out = destination / name
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
        written.append(out)
    (destination / "_config.yml").write_text(CONFIG)
    return written


def main(argv: list[str]) -> int:
    """Build the site's source into the directory named."""
    if len(argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    for page in build(Path(argv[0])):
        print(page)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
