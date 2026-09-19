# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read what a game said about itself while it ran.

A compositor cannot observe which graphics API a client rendered with, and a
demo mode's own frame count is not what the screen presented. Both are things
the game knows and writes down, so this is where a run finds them rather than
guessing or claiming the request it made was honoured.
"""

from __future__ import annotations

import re
from pathlib import Path


def game_reported_rate(output: str) -> float | None:
    """Read the frame rate a game printed itself, where its demo mode prints one."""
    # SuperTuxKart's profile mode ends with a summary naming the frames it drew
    # and the time it took. That is its render throughput, which is not what
    # the screen presented and is reported separately for exactly that reason.
    found = re.search(r"Number of frames:\s*([0-9]+)\s*time\s*([0-9.]+)", output)
    if found and float(found.group(2)) > 0:
        return float(found.group(1)) / float(found.group(2))
    found = re.search(r"FPS\s*[:=]\s*([0-9.]+)", output)
    return float(found.group(1)) if found else None


def game_reported_renderer(output: str, game: str) -> str:
    """Read the graphics API the game says it used, not the one it was asked for.

    A compositor cannot observe this: neither protocol carries a client's
    graphics API and an OpenGL and a Vulkan client hand over the same kind of
    buffer. The game knows, and says so in its own output, so that is where a
    run finds out what it actually measured.
    """
    lines = output.splitlines()
    if game == "supertuxkart":
        # "Using renderer: OpenGL 4.3.0", or Vulkan where that renderer ran.
        found = next((line for line in lines if "Using renderer:" in line), "")
        _, _, named = found.partition("Using renderer:")
        return named.strip()
    found = next((line for line in lines if "renderer" in line.lower()), "")
    return found.strip()


def clear_game_log(game: str) -> None:
    """Remove what the game said last time, so this run reads only its own.

    The log is reopened by the game rather than appended to by this script, so
    a game that exits before replacing it would otherwise hand the previous
    run's renderer and frame count to this one.
    """
    if game == "supertuxkart":
        (Path.home() / ".config/supertuxkart/config-0.10/stdout.log").unlink(missing_ok=True)


def game_log(game: str) -> str:
    """Whatever the game wrote about itself, where it writes to its own file.

    SuperTuxKart reopens its output onto a log of its own within a second of
    starting, so almost nothing reaches the pipe this script holds.
    """
    if game == "supertuxkart":
        path = Path.home() / ".config/supertuxkart/config-0.10/stdout.log"
        if path.exists():
            return path.read_text(errors="replace")
    return ""
