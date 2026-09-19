# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Put an application into a known state before it is measured, and prove it.

A benchmark that does not control the application's own settings measures
whatever the last run left behind. Two things went wrong here on 2026-09-19 for
exactly that reason: a run at Performance stored 1920 x 1080 in SuperTuxKart's
configuration, so every later run began at 1080p whatever the effect asked for;
and the game was sitting at its lowest quality, where a fast machine is bound
by the refresh rate and reducing the resolution can show nothing.

Setting a value is not the same as it being kept. A file can be rewritten by
the application at exit, ignored because the key was spelled for another
version, or simply not exist yet. Everything written here is therefore read
back, and a run says which values it could not establish rather than implying
the whole configuration was under control.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from effect_control import screen_pixels

if TYPE_CHECKING:
    # Only named in an annotation, and this module postpones those.
    from collections.abc import Callable

# SuperTuxKart keeps its settings as attributes of one XML element. These are
# its highest, so the GPU is what a comparison measures: at the lowest settings
# a fast machine reaches the screen's refresh rate at every resolution, and two
# runs then differ by nothing.
SUPERTUXKART_QUALITY = {
    "anisotropic": "16",
    "enable_bloom": "true",
    "enable_dof": "true",
    "enable_dynamic_lights": "true",
    "enable_glow": "true",
    "enable_light_shaft": "true",
    "enable_high_definition_textures": "1",
    "geometry_level": "2",
    "light_scatter": "true",
    "max_texture_size": "2048",
    "mlaa": "true",
    "motionblur_enabled": "true",
    "shadows_resolution": "2048",
    "ssao": "true",
    # Vertical synchronisation off, so the game's own throughput is what its
    # frame time describes rather than the screen's refresh rate.
    "swap-interval": "0",
}


@dataclass
class Outcome:
    """What one attempt to control an application's settings achieved."""

    applied: dict[str, str] = field(default_factory=dict)
    refused: dict[str, str] = field(default_factory=dict)
    note: str = ""

    @property
    def controlled(self) -> bool:
        """Whether every value asked for is the value now in the file."""
        return not self.refused

    def describe(self) -> str:
        """One line for a report, naming what could not be established."""
        if self.note and not self.applied:
            return self.note
        if self.controlled:
            return f"{len(self.applied)} settings verified"
        missing = ", ".join(f"{key}={value}" for key, value in sorted(self.refused.items()))
        return f"{len(self.applied)} verified, not kept: {missing}"


def supertuxkart_config() -> Path:
    """Where SuperTuxKart keeps the settings this module writes."""
    return Path.home() / ".config/supertuxkart/config-0.10/config.xml"


def _write_xml_attributes(path: Path, wanted: dict[str, str]) -> Outcome:
    """Set attributes in an XML configuration, then read them back.

    Only attributes already present are written. A key this version does not
    have is reported as refused rather than invented, because an invented key
    is silently ignored and would be indistinguishable from one that worked.
    """
    outcome = Outcome()
    if not path.exists():
        outcome.note = f"no configuration at {path}"
        outcome.refused = dict(wanted)
        return outcome
    text = path.read_text()
    for key, value in wanted.items():
        pattern = re.compile(rf'(\n\s*{re.escape(key)}=")[^"]*(")')
        text, count = pattern.subn(rf"\g<1>{value}\g<2>", text)
        if not count:
            outcome.refused[key] = value
    path.write_text(text)

    # Read back from disk. A value that did not land is worth more as a
    # reported gap than as an assumption the run was conducted correctly.
    return _read_back_xml(path, wanted, outcome)


def _read_back_xml(path: Path, wanted: dict[str, str], outcome: Outcome | None = None) -> Outcome:
    """Compare a configuration on disk against what was asked of it."""
    found_state = outcome or Outcome()
    if not path.exists():
        found_state.note = f"no configuration at {path}"
        found_state.refused |= dict(wanted)
        return found_state
    written = path.read_text()
    for key, value in wanted.items():
        if key in found_state.refused:
            continue
        found = re.search(rf'\n\s*{re.escape(key)}="([^"]*)"', written)
        if found and found.group(1) == value:
            found_state.applied[key] = value
        else:
            found_state.refused[key] = value
    return found_state


def prepare_supertuxkart(*, native: tuple[int, int] | None = None, output: str = "") -> Outcome:
    """Put SuperTuxKart at its highest quality and at the screen's own size.

    The resolution matters as much as the quality. The game stores the size it
    last ran at and starts the next run from there, so without this one run
    decides what the next one renders and the effect's request is not the only
    thing that changed.
    """
    wanted = dict(SUPERTUXKART_QUALITY)
    size = native or screen_pixels(output)
    if size:
        width, height = size
        wanted |= {
            "real_width": str(width),
            "real_height": str(height),
            "width": str(width),
            "height": str(height),
        }
    return _write_xml_attributes(supertuxkart_config(), wanted)


# Extreme Tux Racer's own settings, by the names its binary carries. Detail and
# the two clip distances are what decide how much it draws; the rest keep a run
# from stopping for a menu or a sound device.
EXTREMETUXRACER_QUALITY = {
    "detail_level": "3",
    "forward_clip_distance": "100",
    "backward_clip_distance": "20",
    "fullscreen": "1",
    "music_volume": "0",
    "sound_volume": "0",
}


def _write_bracket_settings(path: Path, wanted: dict[str, str]) -> Outcome:
    """Set values in a file of "[key] value" lines, then read them back.

    A key the file does not carry is added, because this format is a flat list
    the game rewrites wholesale rather than a schema it validates. Everything
    is read back afterwards regardless.
    """
    outcome = Outcome()
    if not path.exists():
        outcome.note = f"no options at {path}; the game writes them when it exits"
        outcome.refused = dict(wanted)
        return outcome
    lines = path.read_text().splitlines()
    for key, value in wanted.items():
        pattern = re.compile(rf"^\[{re.escape(key)}\]\s.*$")
        replaced = False
        for index, line in enumerate(lines):
            if pattern.match(line):
                lines[index] = f"[{key}] {value}"
                replaced = True
                break
        if not replaced:
            lines.append(f"[{key}] {value}")
    path.write_text("\n".join(lines) + "\n")

    return _read_back_bracket(path, wanted)


def _read_back_bracket(path: Path, wanted: dict[str, str]) -> Outcome:
    """Compare a bracketed options file against what was asked of it."""
    outcome = Outcome()
    if not path.exists():
        outcome.note = f"no options at {path}"
        outcome.refused = dict(wanted)
        return outcome
    written = path.read_text()
    for key, value in wanted.items():
        found = re.search(rf"^\[{re.escape(key)}\]\s+(\S+)", written, re.MULTILINE)
        if found and found.group(1) == value:
            outcome.applied[key] = value
        else:
            outcome.refused[key] = value
    return outcome


def prepare_extremetuxracer() -> Outcome:
    """Put Extreme Tux Racer at its highest detail and a fixed draw distance.

    It writes its options when it exits, so a machine that has never run it to
    completion has no file to set. That is reported rather than worked around:
    a file invented from a guessed format would either be ignored or stop the
    game starting, and the run would look configured when it was not.
    """
    return _write_bracket_settings(Path.home() / ".config/etr/options", EXTREMETUXRACER_QUALITY)


def prepare_benchmark(name: str) -> Outcome:
    """Report that a benchmark keeps no settings to control.

    glmark2 and vkmark take everything on the command line and store nothing
    between runs, which makes them reproducible by construction. That is worth
    stating in a report rather than leaving as a blank.
    """
    return Outcome(note=f"{name} stores no settings; its run is defined by its arguments")


def prepare_glmark2() -> Outcome:
    """Report that glmark2 keeps nothing between runs."""
    return prepare_benchmark("glmark2")


def prepare_vkmark() -> Outcome:
    """Report that vkmark keeps nothing between runs."""
    return prepare_benchmark("vkmark")


PREPARE: dict[str, Callable[[], Outcome]] = {
    "supertuxkart": prepare_supertuxkart,
    "extremetuxracer": prepare_extremetuxracer,
    "glmark2": prepare_glmark2,
    "vkmark": prepare_vkmark,
}


def prepare(game: str, output: str = "") -> Outcome:
    """Put one application into a known state, whatever that means for it."""
    if game == "supertuxkart":
        return prepare_supertuxkart(output=output)
    action = PREPARE.get(game)
    return action() if action else Outcome(note=f"no settings known for {game}")


def still_holds(game: str) -> Outcome:
    """Check an application's settings again once it has exited.

    Both writers verify the file the moment they wrote it, which says nothing
    about what the application does on its way out. Extreme Tux Racer writes
    its options when it quits, so a run that reported settings as verified can
    finish with different ones on disk; this is what catches that.
    """
    if game == "supertuxkart":
        return _read_back_xml(supertuxkart_config(), SUPERTUXKART_QUALITY)
    if game == "extremetuxracer":
        return _read_back_bracket(Path.home() / ".config/etr/options", EXTREMETUXRACER_QUALITY)
    return Outcome(note="nothing stored to check")
