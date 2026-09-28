#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Load a fresh development copy of the installed effect without restarting KWin.

Qt can retain an unloaded plugin's library. A unique filename gives each reload
its own loader entry. Remove that discovery file immediately afterwards so the
temporary effect cannot be loaded automatically at the next login.
"""

from __future__ import annotations

import argparse
import fcntl
import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PREFIX = "upscale_reload_"


def command(arguments: list[str], *, timeout: int | None = 30) -> str:
    """Run an argument vector, keeping failures visible to the desktop caller."""
    result = subprocess.run(arguments, capture_output=True, text=True, check=True, timeout=timeout)
    return result.stdout.strip()


def bus(method: str, *arguments: str) -> str:
    """Call only the running user's KWin effects interface."""
    tool = shutil.which("qdbus6") or shutil.which("qdbus-qt6")
    if not tool:
        message = "Install the Qt 6 qdbus command first."
        raise RuntimeError(message)
    return command([tool, "org.kde.KWin", "/Effects", f"org.kde.kwin.Effects.{method}", *arguments])


def privileged(arguments: list[str], *, gui: bool) -> None:
    """Authorize only copying/removing the temporary plugin, never the script."""
    result = subprocess.run(["sudo", "-n", *arguments], capture_output=True, check=False)
    if result.returncode:
        command(["pkexec" if gui else "sudo", *arguments], timeout=180)


def installed_plugin() -> Path:
    """Use Qt's installation directory instead of assuming a CPU or distribution."""
    directory = command(["qtpaths6", "--query", "QT_INSTALL_PLUGINS"])
    return Path(directory) / "kwin/effects/plugins/upscale.so"


def replace_effect(active: list[str], name: str) -> str:
    """Finish unloading before loading and verifying the replacement."""
    for previous in active:
        bus("unloadEffect", previous)
    remaining = set(bus("loadedEffects").splitlines()).intersection(active)
    if remaining:
        message = f"KWin has not unloaded: {', '.join(sorted(remaining))}"
        raise RuntimeError(message)
    if bus("loadEffect", name) != "true":
        message = "KWin refused the fresh plugin copy."
        raise RuntimeError(message)
    status = bus("supportInformation", name)
    if name not in bus("loadedEffects").splitlines() or "\nbuild: " not in status:
        message = "The fresh plugin did not report its running build."
        raise RuntimeError(message)
    return status


def reload_plugin(source: Path, *, gui: bool) -> str:
    """Replace the active instance; report the build actually running afterwards."""
    source = source.resolve(strict=True)
    if source.name != "upscale.so":
        message = "--plugin must name the installed upscale.so."
        raise ValueError(message)
    active = [
        name
        for name in bus("loadedEffects").splitlines()
        if name == "upscale" or name.startswith(PREFIX)
    ]
    name = PREFIX + uuid.uuid4().hex
    staged = source.with_name(name + ".so")
    install = shutil.which("install")
    remove = shutil.which("rm")
    if not install or not remove:
        message = "The install and rm commands are required."
        raise RuntimeError(message)
    staged_ok = False
    try:
        # Copy before unloading: a cancelled authentication leaves the old
        # effect running. Never overwrite the installed plugin itself.
        privileged([install, "-m", "0644", str(source), str(staged)], gui=gui)
        staged_ok = True
        status = replace_effect(active, name)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError):
        if staged_ok:
            bus("unloadEffect", name)
        if (
            staged_ok
            and active
            and not any(
                item == "upscale" or item.startswith(PREFIX)
                for item in bus("loadedEffects").splitlines()
            )
        ):
            # The normal name may still resolve to an older cached library.
            # This is recovery, never evidence that the requested build loaded.
            restored = bus("loadEffect", "upscale")
            print(f"Recovery: load of the regular plugin returned {restored}.", file=sys.stderr)
        raise
    else:
        return status
    finally:
        if staged.exists():
            privileged([remove, "--", str(staged)], gui=gui)


def desktop_argument(value: str) -> str:
    """Quote an Exec argument for both desktop-entry string and argument parsing."""
    value = value.replace("%", "%%")
    for character in ("\\", '"', "`", "$"):
        value = value.replace(character, "\\" + character)
    return '"' + value.replace("\\", "\\\\") + '"'


def install_launcher(plugin: Path | None) -> str:
    """Install desktop and application-menu entries pointing at this checkout."""
    arguments = [sys.executable, "-B", str(Path(__file__).resolve()), "--gui"]
    if plugin:
        arguments += ["--plugin", str(plugin.resolve(strict=True))]
    content = (
        """# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""
        "[Desktop Entry]\nType=Application\nName=Reload Upscale\n"
        "Comment=Load the installed Upscale build without restarting the desktop\n"
        "Icon=view-refresh\nTerminal=false\nCategories=Development;\n"
        "Exec=" + " ".join(desktop_argument(argument) for argument in arguments) + "\n"
    )
    data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    desktop = Path(command(["xdg-user-dir", "DESKTOP"]))
    destinations = [data / "applications"]
    if desktop.is_absolute() and desktop != Path.home():
        destinations.append(desktop)
    paths = []
    for directory in destinations:
        directory.mkdir(parents=True, exist_ok=True)
        target = directory / "org.kde.upscale.reload.desktop"
        target.write_text(content)
        target.chmod(0o755)
        paths.append(str(target))
    return "Installed Reload Upscale:\n" + "\n".join(paths)


def main() -> int:
    """Provide a command-line reload and a desktop launcher for development."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, help="installed upscale.so, if outside Qt's prefix")
    parser.add_argument("--gui", action="store_true", help="show the outcome in a KDE dialog")
    parser.add_argument(
        "--install-launcher", action="store_true", help="add desktop and menu icons"
    )
    arguments = parser.parse_args()
    work = ROOT / "build/reload-upscale"
    work.mkdir(parents=True, exist_ok=True)
    result = 0
    try:
        # Prevent overlapping double-clicks from leaving two effects active.
        with (work / "reload.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            if arguments.install_launcher:
                message = install_launcher(arguments.plugin)
            else:
                status = reload_plugin(arguments.plugin or installed_plugin(), gui=arguments.gui)
                message = "Upscale reloaded without restarting KWin.\n\n" + status
            (work / "latest.log").write_text(message + "\n")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else ""
        message = f"Reload Upscale failed: {error}\n{detail or ''}"
        result = 1
    print(message)
    if arguments.gui:
        command(
            ["kdialog", "--title", "Reload Upscale", "--error" if result else "--msgbox", message],
            timeout=None,
        )
    return result


if __name__ == "__main__":
    raise SystemExit(main())
