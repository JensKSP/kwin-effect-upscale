#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run SuperTuxKart in a test session and take pictures of what KWin shows.

Started by check-supertuxkart.py as the session's own program, with the cell
in UPSCALE_CASE. It starts the game and waits until the effect says it
enlarges the game's window. Then it stops the game, so that every picture
shows the same frame: the screen, the game's window, and the screen once more
after the effect is unloaded. The game cannot answer being given its size
back while it is stopped, so that last picture is KWin drawing the same
buffer by itself: on Wayland its own plain stretch, and for X11 the buffer
unscaled in the window's corner, from which the plain stretch is computed.
What the effect said, the pictures and their measures go into the result file.
"""

from __future__ import annotations

import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

import output_capture

EFFECTS = ("org.kde.KWin", "/Effects")


def metrics(tool: str) -> dict[str, str]:
    """Read the effect's line for programs, key by key."""
    answer = subprocess.run(
        [tool, *EFFECTS, "org.kde.kwin.Effects.supportInformation", "upscale"],
        capture_output=True,
        text=True,
        check=False,
    )
    for line in answer.stdout.splitlines():
        if line.startswith("metrics: "):
            return dict(entry.partition("=")[::2] for entry in line.split()[1:] if "=" in entry)
    return {}


def enlarging(reading: dict[str, str]) -> bool:
    """Whether the effect says it enlarges SuperTuxKart's window."""
    return (
        "supertuxkart" in reading.get("window", "").lower()
        and reading.get("selected") == "1"
        and reading.get("scaling") == "1"
    )


def stopped_screen(game: subprocess.Popen[bytes], result: dict[str, object]) -> Any:  # noqa: ANN401
    """Stop the game and capture the screen, once more after letting it run on if KWin stalls.

    Twice in some twenty cells on 2026-09-29, KWin drew no frame for over
    two minutes after the game was stopped early in its start, so the capture
    never came; twelve sessions stopped later, with the effect and without,
    captured within a second and a half. A second stop a few seconds on is
    tried once, and the stall is recorded, never hidden.
    """
    for attempt in range(2):
        game.send_signal(signal.SIGSTOP)
        time.sleep(1.0)
        try:
            return output_capture.capture("CaptureWorkspace", timeout=60)
        except Exception as error:
            if attempt:
                raise
            result["stalled"] = f"{type(error).__name__}; stopped again five seconds later"
            game.send_signal(signal.SIGCONT)
            time.sleep(5.0)
    return None


def pictures(case: dict[str, Any], tool: str, game: subprocess.Popen[bytes]) -> dict[str, object]:
    """Stop the game, take the pictures and measure them; record a failure instead of dying."""
    directory = Path(case["directory"])
    result: dict[str, object] = {}
    # Frames drawn at the size the effect has now settled on.
    time.sleep(case["settle"])
    during = metrics(tool)
    result["during"] = during
    try:
        screen = stopped_screen(game, result)
        window = output_capture.capture("CaptureActiveWindow")
        subprocess.run([tool, *EFFECTS, "unloadEffect", "upscale"], check=False)
        time.sleep(3.0)
        after = output_capture.capture("CaptureWorkspace")
    except Exception as error:  # noqa: BLE001 - Any failure is the cell's result.
        result["failure"] = f"{type(error).__name__}: {error}"
        return result
    finally:
        game.send_signal(signal.SIGCONT)
    plain = after
    if during.get("windowsystem") == "x11":
        width, height = (int(side) for side in case["supplied"].split("x"))
        plain = output_capture.enlarged(after, (width, height), (screen.shape[1], screen.shape[0]))
    taken = {"screen": screen, "window": window, "after": after, "plain": plain}
    for name, picture in taken.items():
        output_capture.save(picture, str(directory / f"{name}.png"))
    result["sizes"] = {
        name: f"{picture.shape[1]}x{picture.shape[0]}" for name, picture in taken.items()
    }
    result["sharpness"] = {
        name: output_capture.sharpness(picture) for name, picture in taken.items()
    }
    result["likeness"] = {
        "window": output_capture.likeness(screen, window),
        "plain": output_capture.likeness(screen, plain),
    }
    return result


def main() -> int:
    """Run the game, wait for the effect, take the pictures, and write it all down."""
    case = json.loads(os.environ["UPSCALE_CASE"])
    directory = Path(case["directory"])
    tool = shutil.which("qdbus6") or shutil.which("qdbus") or ""
    environment = dict(os.environ) | case["environment"]
    result: dict[str, object] = {"enlarged": False}
    with (directory / "game.log").open("w") as log:
        game = subprocess.Popen(
            [case["program"], *case["arguments"]],
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + case["seconds"]
            reading: dict[str, str] = {}
            while tool and time.monotonic() < deadline and game.poll() is None:
                reading = metrics(tool)
                if enlarging(reading):
                    break
                time.sleep(1.0)
            result["waited"] = reading
            if enlarging(reading) and game.poll() is None:
                result["enlarged"] = True
                result |= pictures(case, tool, game)
            result["running"] = game.poll() is None
        finally:
            game.terminate()
            try:
                game.wait(timeout=15)
            except subprocess.TimeoutExpired:
                game.kill()
    (directory / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
