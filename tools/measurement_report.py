# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Say what is about to be measured, what it came to, and record both.

A measurement is only worth as much as the record of the conditions it was
taken under, so each run names them before it starts rather than leaving a
reader to reconstruct them from a table of numbers afterwards. The same facts
go into the written record, which is why they are gathered here once.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from pathlib import Path

    from frame_metrics import Summary


@dataclass
class Environment:
    """The conditions every run in a session shares, read rather than assumed."""

    compositor: str = ""
    session: str = ""
    output: str = ""
    output_pixels: str = ""
    refresh: str = ""
    output_scale: str = ""
    variable_refresh: str = ""
    high_dynamic_range: str = ""
    effect_build: str = ""
    # The machine's state while measuring. A run taken under other load, or on
    # a machine that had thermally throttled, is not comparable with one that
    # was not, so what can be read is read and what cannot is said plainly.
    load_average: str = ""
    thermal_state: str = ""
    taken_at: str = field(default_factory=lambda: datetime.now(UTC).isoformat(timespec="seconds"))


def _run(command: list[str]) -> str:
    """Run a helper and return what it said, or nothing if it could not."""
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    return result.stdout


def read_environment(qdbus_tool: str = "qdbus6", output: str = "") -> Environment:
    """Gather the session's fixed conditions from the session itself.

    Takes the D-Bus helper the runner resolved rather than naming one: an
    installation that calls it something else would otherwise record the effect
    as absent while the benchmark ran against it perfectly well.
    """
    found = Environment()
    version = _run(["kwin_wayland", "--version"]).strip()
    found.compositor = version or "unknown"
    found.session = _run(["sh", "-c", "echo $XDG_SESSION_TYPE/$XDG_CURRENT_DESKTOP"]).strip()

    outputs = _run(["kscreen-doctor", "-o"])
    if output:
        # One screen's conditions, named, so a session with several does not
        # have the first one's mode recorded for a game running on another.
        # The name has to end where it ends: a word boundary also matches
        # before a hyphen, so DP-1 would otherwise take DP-1-1.
        blocks = re.split(r"(?=Output:)", outputs)
        named = [
            b for b in blocks if re.search(rf"Output:\s*\d+\s+{re.escape(output)}(?![\w-])", b)
        ]
        if not named:
            # Falling back to the first screen would record conditions for a
            # screen nobody asked about, which is worse than saying so.
            found.output = f"{output} (not found)"
            return found
        outputs = named[0]
    name = re.search(r"Output:\s*\d+\s+(\S+)", outputs)
    found.output = name.group(1) if name else "unknown"
    mode = re.search(r"([0-9]{3,5})x([0-9]{3,5})@([0-9]+)\*", outputs)
    if mode:
        found.output_pixels = f"{mode.group(1)}x{mode.group(2)}"
        found.refresh = f"{mode.group(3)} Hz"
    scale = re.search(r"Scale:\s*([0-9.]+)", outputs)
    found.output_scale = scale.group(1) if scale else "unknown"
    vrr = re.search(r"Vrr:\s*(\S+)", outputs)
    found.variable_refresh = vrr.group(1) if vrr else "unknown"
    hdr = re.search(r"HDR:\s*(\S+)", outputs)
    found.high_dynamic_range = hdr.group(1) if hdr else "unknown"

    status = _run(
        [
            qdbus_tool,
            "org.kde.KWin",
            "/Effects",
            "org.kde.kwin.Effects.supportInformation",
            "upscale",
        ]
    )
    build = re.search(r"build:\s*(.+)", status)
    found.effect_build = build.group(1).strip() if build else "effect not loaded"

    try:
        one, five, fifteen = os.getloadavg()
        found.load_average = f"{one:.2f} {five:.2f} {fifteen:.2f}"
    except OSError:
        found.load_average = "unavailable"
    # No portable way to read this: it lives behind a driver interface that
    # differs per vendor and per platform. Naming it as unavailable keeps the
    # gap visible rather than leaving a reader to assume it was steady.
    found.thermal_state = "not read"
    return found


def announce(number: int, total: int, fields: dict[str, str]) -> None:
    """Say which test is about to run, and under which conditions.

    Printed before the run rather than after, so that a person watching the
    screen knows what they are being asked to judge while they are judging it.
    """
    print(f"\n[{number}/{total}] {fields.get('name', 'run')}")
    for key, value in fields.items():
        if key != "name":
            print(f"      {key:<18} {value}")
    print("      ...", flush=True)


def report(summary: Summary) -> None:
    """Say what one run came to, including what was wrong with it."""

    def figure(value: float | None, digits: int = 1) -> str:
        """Format a number for the shell, or the dash for nothing measured."""
        return "-" if value is None else f"{round(value, digits)}"

    print(f"      supplied           {summary.supplied or '-'} -> {summary.destination or '-'}")
    print(f"      upscaled           {'yes' if summary.scaling else 'NO'}")
    print(
        f"      presented          {figure(summary.presented_rate)}/s"
        f"   frame {figure(summary.frame_time, 2)} ms"
        f"   99th {figure(summary.presented_percentile)} ms"
    )
    print(f"      game committed     {figure(summary.client_updates)}/s")
    if summary.renderer_used:
        print(f"      renderer reported  {summary.renderer_used}")
    print(f"      samples            {summary.samples}")
    for note in summary.notes:
        print(f"      ! {note}")


def write_json(path: Path, environment: Environment, runs: list[dict[str, Any]]) -> None:
    """Write the complete record, conditions included."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"environment": asdict(environment), "runs": runs}, indent=2) + "\n")


def _cell(value: object, digits: int = 1) -> str:
    """Format a value for a table cell, or the dash for one never measured."""
    if value is None or value == "":
        return "—"
    if isinstance(value, bool):
        return "yes" if value else "**no**"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def write_markdown(path: Path, environment: Environment, runs: list[dict[str, Any]]) -> None:
    """Write the same record as a document, with the conditions at the top."""
    path.parent.mkdir(parents=True, exist_ok=True)
    # REUSE-IgnoreStart
    # The lines below are the licence header of the document being written, not
    # of this file. Without this the licence checker reads them as a second,
    # malformed declaration for the source it finds them in.
    lines = [
        "<!--",
        "SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>",
        "SPDX-License-Identifier: GPL-2.0-or-later",
        "-->",
        "",
        "# Frame times with and without a reduced rendering resolution",
        "",
        f"Taken {environment.taken_at}.",
        "",
        "## Conditions",
        "",
        "| | |",
        "| --- | --- |",
        f"| Compositor | {environment.compositor} |",
        f"| Session | {environment.session} |",
        f"| Output | {environment.output}, {environment.output_pixels} at {environment.refresh} |",
        f"| Desktop scale | {environment.output_scale} |",
        f"| Variable refresh | {environment.variable_refresh} |",
        f"| High dynamic range | {environment.high_dynamic_range} |",
        f"| Effect build | {environment.effect_build} |",
        f"| Load average while measuring | {environment.load_average} |",
        f"| Thermal and power state | {environment.thermal_state} |",
        "",
        "## Runs",
        "",
        # REUSE-IgnoreEnd
        (
            "| Application | Window API | Graphics API | Preset | Supplied | Destination"
            " | Upscaled | Direct scanout | Presented /s | Frame ms | 99th ms"
            " | Game /s | Samples |"
        ),
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    lines.extend(
        [
            f"| {run.get('game', '')} | {_cell(run.get('window_system'))}"
            f" | {_cell(run.get('renderer_used'))} | {run.get('preset', '')}"
            f" | {_cell(run.get('supplied'))} | {_cell(run.get('destination'))}"
            f" | {_cell(run.get('scaling'))} | {_cell(run.get('scanout'))}"
            f" | {_cell(run.get('presented_rate'))}"
            f" | {_cell(run.get('frame_time'), 2)} | {_cell(run.get('presented_percentile'))}"
            f" | {_cell(run.get('client_updates'))} | {run.get('samples', 0)} |"
            for run in runs
        ]
    )

    notes = [(run, note) for run in runs for note in run.get("notes", [])]
    if notes:
        lines += ["", "## What was wrong with these runs", ""]
        lines += [
            f"- **{run.get('game', '')} at {run.get('preset', '')}**: {note}" for run, note in notes
        ]

    settings = [
        (run.get("game", ""), run.get("settings", "")) for run in runs if run.get("settings")
    ]
    if settings:
        lines += ["", "## Application settings", ""]
        for game, state in dict(settings).items():
            lines.append(f"- **{game}**: {state}")

    lines += [
        "",
        "## Reading these figures",
        "",
        "The presented rate is what the screen showed, so it cannot exceed the",
        "refresh rate. Where a game draws faster than the screen presents, two runs",
        "both reach that ceiling and say nothing about their rendering cost; the",
        "game's own commit rate is the figure to compare there.",
        "",
        "A run marked as not upscaled did not exercise the effect, whatever else it",
        "measured. Its numbers describe the game at whatever size it chose.",
        "",
    ]
    path.write_text("\n".join(lines))
