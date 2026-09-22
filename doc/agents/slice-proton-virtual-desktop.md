<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Wine and Proton games through a virtual desktop in their prefix

## Status

Written down on Jens's instruction on 2026-09-22, and accepted by him the same
day as the route for Wine and Proton games that ignore resizing, on the
condition that the user is asked first and offered a restart. Implementation
has started; nothing below has been tested in a game yet. Every mechanism is
established from source reading only; the citations name the upstream file and
line in the versions listed under [sources](#sources).

## Start state and evidence

Resolution control ([Resolution control](slice-resolution-control.md)) obtains
smaller buffers from native Wayland games and from X11 games that follow a
resize. Wreckfest (Steam build 16986367, Proton Experimental 11.0-20260917b,
DXVK D3D11, exclusive fullscreen) does not follow: it supplies 3840 × 2160 on a
3840 × 2160 output whatever the effect does. The reason chain, from source:

- DXVK's exclusive fullscreen sets a present rectangle equal to Wine's monitor
  rectangle (DXVK `src/wsi/win32/wsi_window_win32.cpp:206-236`). win32u then
  sizes both the Vulkan swapchain and the X11 window from that rectangle
  (Wine `dlls/win32u/vulkan.c:1762-1791`, `dlls/win32u/window.c:2321-2328`).
  The window's size plays no part.
- Wine's monitor comes from Xwayland's RandR, which is shared by every X11
  program. Xwayland's per-program mode emulation can only be switched on by the
  program's own RandR or VidMode request
  (`hw/xwayland/xwayland-output.c`), and Proton never sends one
  (`emulate_modeset`, `dlls/win32u/sysparams.c:170`, `4623-4636`).
- Upstream Xwayland has no way for a window manager to set another program's
  mode: not in master, not in the 26.1 release candidate, and no merge request
  or issue asks for it (checked 2026-09-21). Debian has 24.1.6 in Trixie and
  24.1.13 in Forky.
- Wine's Wayland driver would take the effect's per-program announcement, but
  none of the installed Valve Protons (Experimental 11.0-20260917b, 11.0-2c,
  10.0-4b) ships it (checked 2026-09-22: only `winex11.so`). Disabling winex11
  leaves the game without any window (`dlls/win32u/driver.c:761-773`).
- No Proton, Wine or DXVK environment setting changes the monitor size the game
  sees, and no Steam-level hook reaches every Proton game without a choice by
  the user. A Vulkan layer is the one hook a package can install that reaches
  every Proton game (pressure-vessel imports host layers), but it cannot change
  the size the game renders at.

## The proposal

A Wine virtual desktop in the game's own prefix makes Wine report the desktop
size as the monitor. Proton gives every Steam game its own prefix
(`steamapps/compatdata/<appid>/pfx`), so there the change reaches only that
game. Other launchers usually keep one prefix per game too; where several games
share a prefix, the question says so and names them.

1. **The game runs once at full size.** The effect recognizes a Wine window
   that it acts on and cannot shrink: the process is a Wine loader
   (`wine64-preloader`, `wine-preloader`, `wine64` or `wine`, whatever the
   flavour's path), and under Proton the class and instance are also
   `steam_app_<appid>`, set by winex11 from `SteamAppId`
   (`dlls/winex11.drv/window.c:1246-1262`).
2. **The effect asks the user** in a centred on-screen display whether it may
   set this game up to render smaller. Nothing is written without a yes.
3. **A companion writes two registry values** into that game's `user.reg`,
   only while no Wine server runs for the prefix:
   `[Software\\Wine\\Explorer] "Desktop"="Default"` and
   `[Software\\Wine\\Explorer\\Desktops] "Default"="<W>x<H>"`, where W × H is
   the size the effect chose. The name must be `Default`: win32u takes the
   virtual desktop's size from `Desktops\Default` only
   (`dlls/win32u/sysparams.c:2889-2903`, `2944-2951`).
4. **The effect offers to restart the game** in the same centred display, so
   the change takes effect at once instead of at the next start.
5. **From the next start** every process in the prefix sees a W × H monitor.
   The largest mode is W × H, so even a stored 3840 × 2160 lands on W × H
   (`FindClosestMatchingMode1`, DXVK `src/dxgi/dxgi_swapchain.cpp:849-885`;
   `add_modes`, `dlls/win32u/sysparams.c:2323-2371`). Xwayland hands KWin one
   toplevel, the "Wine Desktop" window, whose buffer is W × H
   (`hw/xwayland/xwayland-window.c:1349-1385`).
6. **The effect enlarges it**: it makes the window fullscreen in KWin while
   holding its X size at W × H, the sequence its X11 path already uses
   (`x11resolution_events.cpp`), upscales with FSR 1 and maps input.

Nothing in Proton resets these values. The proton script edits only
`system.reg`, never mentions a desktop, and copies default files only where
none exist (proton script `copy_pfx`). A downgrade of Proton, `destroyprefix`
or a new prefix removes them. The companion must therefore check again, never
assume.

An earlier probe on Trixie's Wine agrees: a virtual desktop supplied the small
buffer in a decorated, non-fullscreen window, and letting the window grow to the
screen grew the buffer to 4K
([probe](slice-resolution-control.md#source-led-compatibility-investigations)).
Wine ignores window-manager resizes of a virtual desktop
(`dlls/winex11.drv/window.c:2018-2027`), but the X window itself still grows,
which is why the effect has to hold its size.

## Additions laid down by Jens, 2026-09-22

1. **The write must provably reach the right place for this user.** The
   companion proves, programmatically, that the prefix it edits is the one the
   running game uses and belongs to the session's user. It never derives the
   path from Steam library folders or guesses it. See
   [locating the prefix](#locating-the-prefix).
2. **The user is asked first.** The effect asks before anything is written,
   and it says that the change lives in the game's Wine prefix and remains if
   the effect is uninstalled, until it is reset.
3. **The effect offers to restart the game** so the change takes effect. This
   uses a new on-screen display shown in the centre of the screen, only when a
   question or an offer is pending.
4. **Any Wine and Proton flavour.** Valve's Proton, GE-Proton and other Proton
   builds, distribution and upstream Wine, Wine-GE and the launchers that run
   them (Steam, Lutris, Heroic, Bottles, a plain `wine` command), installed
   natively or as Flatpak or Snap. Nothing in the mechanism may depend on one
   flavour's paths or helpers; flavour-specific steps are extras on top of the
   generic ones.

## Locating the prefix

Every step must hold, or the companion writes nothing and the display says why.
The steps use only what every Wine has: its process, its environment, its
prefix and its server lock.

- The window's process id comes from KWin (XRes or `_NET_WM_PID`, which winex11
  sets, `dlls/winex11.drv/window.c:1266-1269`). pressure-vessel shares the PID
  namespace, so the id is valid on the host; for Flatpak and Snap this is
  unverified.
- The process belongs to the session's user (the owner of `/proc/<pid>`).
- The prefix is `WINEPREFIX` from the process's environment
  (`/proc/<pid>/environ`, same-user read), or `$HOME/.wine` when it is unset,
  Wine's own default. Every path is resolved inside the process's own view of
  the file system, through `/proc/<pid>/root`, so a container (pressure-vessel,
  Flatpak, Snap) is seen as the game sees it. Whether that access is allowed
  under every Yama setting is unverified.
- Under Proton, as an extra cross-check: after canonicalisation `WINEPREFIX`
  equals `$STEAM_COMPAT_DATA_PATH/pfx`, the directory name of
  `STEAM_COMPAT_DATA_PATH` equals `SteamAppId`, and that id equals the one in
  the window class.
- The prefix directory and `user.reg` are owned by the user; `user.reg` is a
  regular file, not a symbolic or hard link, and starts with
  `WINE REGISTRY Version 2`.
- While the game runs, the Wine server's lock for exactly this prefix is held:
  `<tmp>/.wine-<uid>/server-<st_dev>-<st_ino>/lock`, where the numbers are the
  prefix directory's device and inode (Wine `server/request.c:675-686`) and
  `<tmp>` is the game's own `/tmp`, seen through `/proc/<pid>/root`. That ties
  the file on disk to the running game, in every flavour.

## Writing and undoing

- Write only when no Wine server holds the prefix's lock. Test it with
  `F_GETLK`, never take it: a server that cannot take its lock exits
  (`server/request.c:745-764`, `799-833`). An edit made while the game runs is
  lost, because the server rewrites `user.reg` on exit and every 30 seconds
  (`server/registry.c:1894-1979`, `2164-2219`).
- Under Proton, also hold its own `compatdata/<appid>/pfx.lock` while writing,
  so a launch in the meantime waits (proton script `FileLock(..., timeout=-1)`).
  Other flavours have no such lock; there the server lock is checked again
  after the write, and the write is repeated after the next exit if a server
  started in between.
- Replace the file atomically (temporary file, then rename), change only the
  two keys, leave every other byte alone, and check the server lock again
  afterwards.
- Never overwrite a `Desktop` value the user set (winecfg, protontricks). The
  companion keeps its own record of the prefixes it changed, in
  `$XDG_STATE_HOME`, and undoes only its own change.
- Undo when the user declines later, turns the effect off for that game or
  changes the size: at once for a game that is not running, after exit for one
  that is.
- The settings page lists the games set up this way and offers to reset them.
- **Uninstalling cannot undo it.** Once the package is gone nothing runs as the
  user, and a root maintainer script cannot safely edit per-user prefixes. The
  consent text and the package description say so.

## The centred display

A new interactive on-screen display, centred on the game's output, shown only
while a question or an offer is pending. It is owned by
[What the effect says](slice-development-infrastructure.md), which owns the
passive and interactive displays; this slice supplies its two uses:

- **The question:** what will change (the game renders at W × H inside a Wine
  desktop that the effect enlarges), where it is stored, that it remains after
  uninstalling until reset, and the answers *Set up*, *Not now* and *Never for
  this game*. The answer is remembered per game.
- **The restart offer:** *Restart game and apply* or *Later*, with the warning
  that closing the game may lose unsaved progress. It follows the handbook's
  [restarting a game with pending settings](../upscaling.md#restarting-a-game-with-pending-settings):
  the restart happens only on the user's choice. The game is closed, the
  companion writes after the Wine server has exited, and the game is started
  again the way it was started: through Steam with `steam://rungameid/<appid>`,
  through the launcher that started it where that launcher offers a way, and
  otherwise with the recorded command line, environment and working directory.
  All three are unverified; where none is known, the display says to start the
  game again instead of offering the restart.

It must work with keyboard, mouse and controller, because the game may hold the
pointer.

## Changes in the effect

From source reading of the effect's X11 path; none of it exists yet.

1. Recognise the Wine desktop window: class `steam_app_<digits>` (or
   `steam_proton`), caption "Wine Desktop", executable `wine64-preloader`.
2. Request without RandR negotiation: the size wanted is the window's own X
   size; treat the current buffer as the answer
   (`x11resolution.cpp`, `x11resolution_validate.cpp`).
3. Make the window cover the output from the effect's side: fullscreen in KWin
   while the X size stays pinned, because Wine asks for fullscreen only when its
   X size equals a monitor.
4. Restore also leaves KWin's fullscreen state (`restore()` does not today).
5. Pointer lock and confinement: KWin tests the lock region in unscaled
   surface coordinates (KWin `src/window.cpp:390-393`,
   `src/pointer_input.cpp:647-760`), so a lock engages only in the top-left
   W × H of the frame. This also affects the existing L4D2 path.
6. When the game changes mode itself, Wine may resize the desktop window
   (`dlls/win32u/defwnd.c:3148-3170`). Whether it arrives at the virtual or the
   raw size is not settled from source; the experiment below settles it.
7. The prefix check, the question and the restart offer reach the companion
   through an interface that keeps `src/plugins/upscale/` free of
   project-specific code (open decision).

## What the player notices

- The first run is at full size, until the question is answered.
- A blank "Wine Desktop" appears at launch before the game draws, and flickers
  once when it becomes fullscreen.
- Launchers, dialogs and error boxes appear inside the enlarged desktop.
- Wine ignores focus loss in desktop mode, so the game is not told when the
  user switches away (`dlls/winex11.drv/event.c:960-967`).

## Scope and exclusions

In scope: every Wine and Proton flavour listed under the
[additions](#additions-laid-down-by-jens-2026-09-22), through winex11 on
Xwayland, in exclusive fullscreen and borderless.

A flavour that runs the game on Wine's Wayland driver (upstream Wine with
`winewayland`, GE-Proton's Wayland switch) needs no prefix change: the game is
a Wayland client and hears the effect's announcement at start. There, however,
the driver keeps its fullscreen window at the announced size instead of the
output's (`dlls/winewayland.drv/window.c:564-571`), and the effect refuses a
window that does not cover the output. Presenting it is part of this slice; the
virtual desktop is not used there, since that driver has no virtual desktop.

Excluded: native Linux games (other paths own them). Open: the BSDs, where Wine
runs but the process inspection above uses Linux's `/proc`; until a BSD
implementation exists the companion is built on Linux only and the effect
behaves as without it.

## Gates

- **Supported scope:** Wreckfest (DXVK D3D11, exclusive fullscreen) with Proton
  Experimental 11 on Debian Trixie's KWin 6.3.6, and one game under Wine outside
  Steam: the question, the restart, a W × H buffer upscaled across the output
  with working input, and a clean undo from the settings page. The status names
  every flavour not yet verified.
- **Full acceptance:** every flavour listed under the additions, including
  Flatpak and Snap installs and Wine's Wayland driver, across the handbook's
  matrix (D3D9, D3D11, D3D12, OpenGL, Vulkan; exclusive and borderless) on real
  hardware, with automated tests for the prefix check, the write and the undo,
  and the X11 path's pinned presentation.

## Acceptance criteria

- The companion refuses every mismatch in [locating the prefix](#locating-the-prefix),
  each covered by a test with a fabricated prefix and environment.
- No write happens without a yes, while the game's Wine server runs, or into a
  prefix whose `Desktop` value the user set.
- After *Restart game and apply*, the game restarts through Steam and supplies
  W × H; the effect shows it fullscreen and upscaled.
- Reset from the settings page restores the prefix byte for byte except the two
  keys, and the next start is at full size.

## Planned experiment (not run)

A one-off manual check before any code:

1. Close Wreckfest and wait until its Wine server has exited. Back up
   `/srv/games/steam-jens/steamapps/compatdata/228380/pfx/user.reg`.
2. Append the two keys with `"Default"="2560x1440"`.
3. Start the game normally. Expected: a decorated 2560 × 1440 "Wine Desktop"
   window with class `steam_app_228380` and the game inside; 2560 × 1440 is the
   largest mode in the game's settings.
4. Make it fullscreen from KWin's window menu. Expected: the buffer grows to
   3840 × 2160 with the game in the top-left 2560 × 1440.
5. Pick 1920 × 1080 in the game and note whether the desktop window shrinks.
6. Quit, wait for the Wine server to exit, restore the backup.

## Progress

| Step | State |
| --- | --- |
| Registry edit: set and remove the two values in `user.reg` text, leaving every other byte (`src/winedesktop/wineregistry.cpp`) | Done. `upscale-wine-registry` passes in the Trixie container with GCC and Clang, warnings as errors; Neon not run |
| Server lock check (`F_GETLK` on the prefix's lock, and the holder's process so the wait outlives the game's view of the file system) (`wineserverlock.cpp`) | Done |
| Locating the prefix from the game's process, with every check above (`wineprefix.cpp`; Linux `/proc` in `wineprocess_proc.cpp`, nothing elsewhere) | Done |
| Setting and undoing the desktop after the game exited: proven directory only, under Proton's `pfx.lock`, atomic, the user's own desktop left alone (`winedesktopwrite.cpp`) | Done. `upscale-wine-prefix` and `upscale-wine-desktop-write` pass in the Trixie container with GCC and Clang, warnings as errors; Neon not run |
| Companion service, its record of changed prefixes and the undo | Next |
| Centred display: question and restart offer | Open, with [What the effect says](slice-development-infrastructure.md) |
| Effect: recognise, pin and present the Wine desktop window | Open |
| Manual Wreckfest experiment | Not run |

## Decisions and open questions

- Accepted by Jens, 2026-09-22: the route itself, with the user's consent
  before any write and an offer to restart the game. The handbook's fourth
  requirement and its ruling on "the private virtual desktop" have to be
  updated to say so: the consent is what allows a change that outlives
  uninstalling, and this form works through configuration, not at start.
- Open: where the companion lives and how the plugin talks to it without
  project-specific code in `src/plugins/upscale/`.

## Sources

Read locally, not vendored: Proton's Wine at commit dc26e61 (Proton 11),
the proton script of Proton Experimental 11.0-20260917b, DXVK 3.1.1,
Xwayland 24.1.6, KWin 6.3.6, steam-runtime-tools v0.20260805.0 and
plasma-workspace 6.3.6.
