# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Set the effect's own settings for a run, and ask it what it sees.

Only the effect's settings are touched, never the session's, and only for the
duration of a run. Separated from the script that starts games because talking
to a running compositor is a different job from launching a process and waiting
for it, and because this half can be read without the other.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

GROUP = "Effect-upscale"
PRESETS = {
    "automatic": 0,
    "native": 1,
    "ultra-quality": 2,
    "quality": 3,
    "balanced": 4,
    "performance": 5,
    "custom": 6,
}

# What a preset asks for, as a fraction of the output. Reported alongside the
# measurement so a table says what was rendered, not only which word was set.
RATIOS = {
    "native": 1.0,
    "ultra-quality": 1.0 / 1.3,
    "quality": 1.0 / 1.5,
    "balanced": 1.0 / 1.7,
    "performance": 0.5,
}


def run_command(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    """Run a helper and return it, without raising on a non-zero exit."""
    return subprocess.run(arguments, capture_output=True, text=True, check=False)


def qdbus() -> str:
    """Find the Qt D-Bus helper this session has, under either of its names."""
    for name in ("qdbus6", "qdbus-qt6", "qdbus"):
        found = shutil.which(name)
        if found:
            return found
    missing = "no qdbus binary found; install qt6-tools or qttools5-dev-tools"
    raise SystemExit(missing)


def status(tool: str) -> str:
    """Ask the running effect for its current state, accumulated."""
    result = run_command(
        [tool, "org.kde.KWin", "/Effects", "org.kde.kwin.Effects.supportInformation", "upscale"]
    )
    return result.stdout


def configure(preset: str, *, sharpening: bool) -> str:
    """Set the effect's own settings for the next run and apply them.

    These are the effect's own settings, not the session's: the preset under
    test, resolution control and the sharpening state. The on-screen display is
    left exactly as the user had it, because measuring does not depend on it
    and showing it would cost every run the same composition it saves.

    Returns what went wrong, or nothing. A setting that was not written, or an
    effect that was never told to re-read them, leaves the run measuring the
    preset before it: the worst kind of failure here, because it produces a
    number rather than an error.
    """
    settings = {
        "Preset": str(PRESETS[preset]),
        "Sharpening": "true" if sharpening else "false",
    }
    for key, value in settings.items():
        written = run_command(
            ["kwriteconfig6", "--file", "kwinrc", "--group", GROUP, "--key", key, value]
        )
        if written.returncode != 0:
            return f"could not set {key}={value}: {written.stderr.strip() or 'no reason given'}"
    applied = run_command(
        [qdbus(), "org.kde.KWin", "/Effects", "org.kde.kwin.Effects.reconfigureEffect", "upscale"]
    )
    if applied.returncode != 0:
        return f"the effect was not told to re-read its settings: {applied.stderr.strip()}"

    # Written is not the same as in force. The effect re-reads on request, and
    # a request that was accepted can still leave an older value if the write
    # landed after it, so the value is read back from where the effect reads it.
    read = run_command(["kreadconfig6", "--file", "kwinrc", "--group", GROUP, "--key", "Preset"])
    if read.stdout.strip() != str(PRESETS[preset]):
        return f"the preset is {read.stdout.strip() or 'unset'}, not {preset}"
    return ""


def reset_game_resolution(game: str, output: str = "") -> str:
    """Put the game back to the screen's own size before a run.

    A game that stores the resolution it last ran at starts the next run from
    there, so one run decides what the next one renders: measured 2026-09-19,
    a run at Performance left 1920 x 1080 in SuperTuxKart's configuration and
    every later run began at 1080p whatever the effect advertised. Starting
    each run at the screen's size makes the effect's request the only thing
    that can change it.

    Returns what was changed, for the record, or why nothing was.
    """
    if game != "supertuxkart":
        return "not configurable here"
    path = Path.home() / ".config/supertuxkart/config-0.10/config.xml"
    if not path.exists():
        return "no configuration yet"
    native = screen_pixels(output)
    if not native:
        return "screen size unknown"
    width, height = native
    text = path.read_text()
    for key, value in (
        ("real_width", width),
        ("real_height", height),
        ("width", width),
        ("height", height),
    ):
        text = re.sub(rf'(\n\s*{key}=")[^"]*(")', rf"\g<1>{value}\g<2>", text)
    path.write_text(text)
    return f"{width}x{height}"


def screen_pixels(output: str = "") -> tuple[int, int] | None:
    """Read one screen's size in pixels, where a game should start.

    Names the screen rather than taking the first current mode listed. A
    session with more than one has more than one, and sizing a game for a
    screen it is not on is a run that measured the wrong thing.
    """
    text = run_command(["kscreen-doctor", "-o"]).stdout
    if output:
        blocks = re.split(r"(?=Output:)", text)
        # The name ends where it ends: a word boundary also matches before a
        # hyphen, so DP-1 would otherwise take DP-1-1.
        named = [
            block
            for block in blocks
            if re.search(rf"Output:\s*\d+\s+{re.escape(output)}(?![\w-])", block)
        ]
        if not named:
            return None
        text = named[0]
    found = re.search(r"([0-9]{3,5})x([0-9]{3,5})@[0-9]+\*", text)
    return (int(found.group(1)), int(found.group(2))) if found else None
