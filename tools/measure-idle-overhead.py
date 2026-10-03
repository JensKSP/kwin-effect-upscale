#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure what the loaded effect costs a client it has nothing to do for.

    measure-idle-overhead.py [--pairs 6] [--plugin PATH] [--report FILE]

glmark2 runs fullscreen at the output's own size, with the effect at Native,
so the effect leaves the window alone and the output keeps direct scanout.
Every difference between the effect unloaded (A0) and loaded (A1) is then what
it costs merely to be there. The pairs alternate their order, A0 A1, A1 A0,
so that drift over the run weighs on both alike, and nothing reads the effect
during a run. The answer is per scene: the medians of both, their ratio, and
the frame time each pair added.

Run it in the session to measure, on its own output, with nothing else running.
It refuses to start while the machine is busy, since a build or a virtual
machine beside it turns the pairs into noise (it did on 2026-10-03), and stops
at the first run glmark2 does not finish, with what glmark2 said:
the effect's settings are set to Native with the displays off for the run and
put back afterwards, and the screen saver is held off meanwhile. --plugin loads
another build of the effect for the run under a name of its own, as
reload-upscale.py does, and the installed one again afterwards.

This is the measurement of the FSR slice's gate: 0.88 to 0.90 of the unloaded
throughput, 4.9 microseconds a frame, before the effect stopped reading its
configuration at every commit, and 0.98 to 0.99, 0.4 microseconds, after.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import re
import statistics
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

from effect_control import GROUP, qdbus, run_command

if TYPE_CHECKING:
    from collections.abc import Iterator

SCENES = ("texture", "shading", "build")
# What the run changes, and what each key is set to for it. Native leaves the
# window alone; the displays are composited content and would hold the output
# in composition, which is exactly the cost this must not measure.
RUN_SETTINGS = {"Resolution": "0", "Osd": "false"}
FPS = re.compile(r"\[(\w+)\].*FPS:\s*(\d+)")
# The load average above which the machine is too busy to measure on.
BUSY = 2.0
# What kreadconfig6 answers for a key that is not stored, which no value is.
UNSET = "measure-idle-overhead: not stored"


@dataclass
class Run:
    """One glmark2 run: its pair, whether the effect was loaded (A1) or not (A0), its rates."""

    pair: int
    label: str
    fps: dict[str, int] = field(default_factory=dict)


def parse_fps(output: str) -> dict[str, int]:
    """Read glmark2's frames per second for each scene it ran."""
    return {found.group(1): int(found.group(2)) for found in FPS.finditer(output)}


def frame_time_deltas(runs: list[Run]) -> dict[str, list[float]]:
    """Name, per scene, the microseconds the loaded effect added in each pair."""
    deltas: dict[str, list[float]] = {}
    for pair in sorted({run.pair for run in runs}):
        found = {run.label: run.fps for run in runs if run.pair == pair}
        unloaded, loaded = found.get("A0", {}), found.get("A1", {})
        for scene in SCENES:
            if unloaded.get(scene) and loaded.get(scene):
                delta = 1e6 / loaded[scene] - 1e6 / unloaded[scene]
                deltas.setdefault(scene, []).append(delta)
    return deltas


def summarize(runs: list[Run]) -> dict[str, object]:
    """Put the pairs together: medians, ratio and the paired cost per scene and over all."""
    scenes: dict[str, object] = {}
    deltas = frame_time_deltas(runs)
    for scene in SCENES:
        rates = {
            label: [run.fps[scene] for run in runs if run.label == label and run.fps.get(scene)]
            for label in ("A0", "A1")
        }
        if not rates["A0"] or not rates["A1"]:
            continue
        unloaded, loaded = statistics.median(rates["A0"]), statistics.median(rates["A1"])
        scenes[scene] = {
            "unloaded": unloaded,
            "loaded": loaded,
            "ratio": round(loaded / unloaded, 3),
            "added_microseconds_median": round(statistics.median(deltas[scene]), 1),
        }
    every = [value for values in deltas.values() for value in values]
    return {
        "scenes": scenes,
        "added_microseconds_median": round(statistics.median(every), 1) if every else None,
        "added_microseconds_range": [round(min(every), 1), round(max(every), 1)] if every else None,
        "pairs": len({run.pair for run in runs}),
    }


def effects(*arguments: str) -> str:
    """Ask KWin's effects interface, and answer what it said."""
    return run_command([qdbus(), "org.kde.KWin", "/Effects", *arguments]).stdout.strip()


def loaded(name: str) -> bool:
    """Whether KWin has an effect of this name loaded now."""
    return name in effects("org.kde.kwin.Effects.loadedEffects").split()


def switch(name: str, *, on: bool) -> None:
    """Load or unload an effect, and give KWin a moment to settle."""
    if loaded(name) != on:
        method = "loadEffect" if on else "unloadEffect"
        effects(f"org.kde.kwin.Effects.{method}", name)
    time.sleep(3.0)


@contextlib.contextmanager
def run_settings() -> Iterator[None]:
    """Set the effect to Native with its displays off, and put back what was there."""
    read = ["kreadconfig6", "--file", "kwinrc", "--group", GROUP, "--default", UNSET, "--key"]
    before = {key: run_command([*read, key]).stdout.rstrip("\n") for key in RUN_SETTINGS}
    try:
        for key, value in RUN_SETTINGS.items():
            run_command(
                ["kwriteconfig6", "--file", "kwinrc", "--group", GROUP, "--key", key, value]
            )
        effects("org.kde.kwin.Effects.reconfigureEffect", "upscale")
        yield
    finally:
        for key, value in before.items():
            restore = [value] if value != UNSET else ["--delete"]
            run_command(
                ["kwriteconfig6", "--file", "kwinrc", "--group", GROUP, "--key", key, *restore]
            )
        effects("org.kde.kwin.Effects.reconfigureEffect", "upscale")


@contextlib.contextmanager
def screen_saver_held() -> Iterator[None]:
    """Keep the screen from blanking or locking for as long as this lasts."""
    holder = subprocess.Popen(["kde-inhibit", "--power", "--screenSaver", "sleep", "86400"])
    try:
        yield
    finally:
        holder.terminate()
        holder.wait(timeout=10)


@contextlib.contextmanager
def effect_from(plugin: Path | None) -> Iterator[str]:
    """Yield the name to measure: the loaded effect, or a build loaded for the run."""
    if plugin is None:
        yield "upscale"
        return
    directory = Path(run_command(["qtpaths6", "--query", "QT_INSTALL_PLUGINS"]).stdout.strip())
    name = "upscale_reload_" + uuid.uuid4().hex
    staged = directory / "kwin/effects/plugins" / f"{name}.so"
    # KWin finds an effect by file name in its plugin directory, so the build
    # is placed there under a name of its own, never over the installed one.
    source = str(plugin.resolve(strict=True))
    run_command(["sudo", "-n", "install", "-m", "0644", source, str(staged)])
    try:
        switch("upscale", on=False)
        switch(name, on=True)
        if not loaded(name):
            message = f"KWin did not load {plugin}"
            raise RuntimeError(message)
        yield name
    finally:
        switch(name, on=False)
        switch("upscale", on=True)
        run_command(["sudo", "-n", "rm", "-f", "--", str(staged)])


def run_glmark2(seconds: int) -> dict[str, int]:
    """Run glmark2 fullscreen through the three scenes and read its rates, or fail with why."""
    command = ["glmark2-wayland", "--fullscreen"]
    for scene in SCENES:
        command += ["-b", f"{scene}:duration={seconds}"]
    done = run_command(command)
    rates = parse_fps(done.stdout)
    if set(rates) != set(SCENES):
        said = (done.stderr.strip() or done.stdout.strip())[-400:]
        message = f"glmark2 exited {done.returncode} with rates for {sorted(rates)}: {said}"
        raise RuntimeError(message)
    return rates


def busy() -> float:
    """Read the machine's load over the last minute."""
    return float(Path("/proc/loadavg").read_text().split()[0])


def measure(name: str, pairs: int, seconds: int) -> list[Run]:
    """Run the pairs, alternating their order, and keep each run's rates."""
    runs: list[Run] = []
    switch(name, on=True)
    run_glmark2(seconds)
    for pair in range(pairs):
        for label in ("A0", "A1") if pair % 2 == 0 else ("A1", "A0"):
            switch(name, on=label == "A1")
            runs.append(Run(pair, label, run_glmark2(seconds)))
            print(json.dumps(asdict(runs[-1])), flush=True)
    switch(name, on=True)
    return runs


def main(argv: list[str] | None = None) -> int:
    """Measure, and print and keep the summary."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--pairs", type=int, default=6, help="pairs of runs, alternating their order"
    )
    parser.add_argument("--seconds", type=int, default=8, help="length of each scene")
    parser.add_argument(
        "--plugin", type=Path, help="another build's upscale.so, loaded for the run"
    )
    parser.add_argument(
        "--report", type=Path, help="where to keep the runs and the summary as JSON"
    )
    options = parser.parse_args(argv)
    if (load := busy()) > BUSY:
        print(f"the machine is busy (load {load:.1f}); measure when nothing else runs")
        return 2
    try:
        with screen_saver_held(), run_settings(), effect_from(options.plugin) as name:
            runs = measure(name, options.pairs, options.seconds)
    except RuntimeError as error:
        # The runs so far were printed as they finished; the settings, the
        # effect and the screen saver are back as they were.
        print(f"stopped: {error}")
        return 1
    summary = summarize(runs)
    print(json.dumps(summary, indent=1))
    if options.report:
        report = {"runs": [asdict(run) for run in runs], "summary": summary}
        options.report.write_text(json.dumps(report, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
