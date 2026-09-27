# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run one game inside a session and record what the effect said about it.

This runs as the compositor's own session command, because the bus the effect
registers on belongs to that session. A reader started outside it finds no
effect to ask and reports every case as unseen, which is exactly what happened
on 2026-09-27 before this half existed: the session had done its work and the
harness could not see it.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from pathlib import Path

FIELDS = {"presentation": str, "selected": bool, "supplied": str, "window": str}


def read_effect(tool: str) -> dict[str, object]:
    """Ask the effect what it decided, from the line written for programs."""
    answer = subprocess.run(
        [tool, "org.kde.KWin", "/Effects", "org.kde.kwin.Effects.supportInformation", "upscale"],
        capture_output=True,
        text=True,
        check=False,
    )
    found: dict[str, object] = {}
    for entry in answer.stdout.split():
        key, separator, value = entry.partition("=")
        if separator and key in FIELDS:
            found[key] = value == "1" if FIELDS[key] is bool else value
    return found


def names_the_game(reading: dict[str, object], window: str) -> bool:
    """Whether this reading is about the game's own window rather than another."""
    return window.lower() in str(reading.get("window", "")).lower()


def watch(
    tool: str, game: subprocess.Popen[bytes], seconds: float, window: str
) -> dict[str, object]:
    """Wait for the effect to describe the game's window, or for time to run out.

    A reading about another window is not an answer about this game. The effect
    describes whichever window it is looking at, and early in a run that is
    something else entirely: on 2026-09-27 a windowed game was read as
    "wayland-fullscreen" because the reading taken was about a window the game
    had not made. One that names this game's window is an answer even where the
    effect declined to act on it, and it is kept in case nothing better follows.
    """
    found: dict[str, object] = {"presentation": "", "selected": False, "supplied": ""}
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline and game.poll() is None:
        reading = read_effect(tool)
        if reading.get("presentation") and names_the_game(reading, window):
            found |= reading
            if found["selected"]:
                break
        time.sleep(1.0)
    return found


def main() -> int:
    """Start the game, watch the effect, and write down what it said."""
    case = json.loads(os.environ["UPSCALE_CASE"])
    # An empty value removes the variable rather than setting it to nothing,
    # which is how a game is denied a protocol it would otherwise prefer.
    environment = dict(os.environ) | case["environment"]
    for name, value in case["environment"].items():
        if not value:
            environment.pop(name, None)
    tool = shutil.which("qdbus6") or shutil.which("qdbus") or ""
    with Path(case["log"]).open("w") as log:
        game = subprocess.Popen(
            [case["program"], *case["arguments"]],
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            found = watch(tool, game, case["seconds"], case["window"]) if tool else {"tool": False}
        finally:
            game.terminate()
            try:
                game.wait(timeout=15)
            except subprocess.TimeoutExpired:
                game.kill()
    Path(case["result"]).write_text(json.dumps(found))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
