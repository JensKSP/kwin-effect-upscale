<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Wine and Proton games through a virtual desktop in their prefix

## Status

Written down on Jens's instruction on 2026-09-22, and accepted by him the same
day as the route for Wine and Proton games that ignore resizing, on the
condition that the user is asked first and offered a restart. The route is
implemented and ran in a session with Wreckfest on 2026-09-23: everything the
effect and the companion do worked, and the route still did not hold that game,
because a virtual desktop does not keep a game from choosing a larger mode; see
[why a virtual desktop does not hold every game](#why-a-virtual-desktop-does-not-hold-every-game).
An additional route for such games is open: Jens rejected every launch-time one
as too invasive, and asks for the least invasive and most generic mechanism
there is. The citations name the upstream file and line in the versions listed
under [sources](#sources).

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
5. **From the next start** every process in the prefix sees a W × H monitor,
   and a game that takes that resolution renders at it. A game that keeps a
   larger one in its own settings does not: the modes offered inside the
   desktop still reach the output's own resolution, so it asks for that and the
   desktop is grown to it; see
   [why a virtual desktop does not hold every game](#why-a-virtual-desktop-does-not-hold-every-game).
   For the first kind, Xwayland hands KWin one toplevel, the "Wine Desktop"
   window, whose buffer is W × H (`hw/xwayland/xwayland-window.c:1349-1385`).
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
  so a Proton launch waits (proton script `FileLock(..., timeout=-1)`).
- Reach the prefix through its directory, held open since it was proven while
  the game ran and checked against the proven device and inode before every
  write. The registry is read, written as a new file and renamed over the old
  one relative to that directory (`openat`, `renameat`), so the write reaches
  the proven directory whatever paths the host has, and a directory removed in
  the meantime is refused. Without a held directory, for a reset after the
  helper restarted, the game's path is opened and checked the same way.
- The lock test and the rename are not one step: a server of another launch
  can start in between. Two things cover that. The lock is tested again after
  the rename, and where a server is seen the write is repeated after that run.
  And every time the prepared game starts, the helper checks that the prefix
  still holds the desktop; one that was lost, because an overlapping server
  saved its own copy on exit or a Proton downgrade rebuilt the prefix, is
  written again after that run. Taking the server lock instead would make the
  overlapping launch fail, since a server that finds its lock taken exits.
- Change only the two keys and leave every other byte alone.
- Never overwrite a `Desktop` value the user set (winecfg, protontricks). The
  companion keeps its own record of the prefixes it changed, in
  `$XDG_STATE_HOME`, and undoes only its own change.
- Undo when the user resets it on the settings page: at once for a game that
  is not running, after exit for one that is. Follow the settings when the
  game's window next appears: the effect tells the helper the size it wants
  now, and a changed size is written, or the desktop undone when the effect no
  longer acts on the game, after that run.
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
| Companion service `kwin-upscale-helper`: D-Bus activated as `org.kde.KWin.Upscale.Helper`, implementing the plugin's optional `org.kde.KWin.Upscale.Helper1` interface; its record of prepared prefixes in the user's state directory; waiting for the game and its server, writing, restarting through Steam, presenting, reset and "never" (`winedesktophelper.cpp`, `winedesktopservice.cpp`) | Done. `upscale-wine-desktop-helper` runs it against real processes and a held server lock; all four companion tests pass 20 times in a row in the Trixie container with GCC and Clang. The service, its D-Bus activation file and its user unit install; the RPM recipe lists them. Neon not run |
| Centred display: question and restart offer (`question.cpp`, `preparation.cpp`; the selected answer in Breeze's highlight colour) | Done. It holds the keyboard (arrow keys and Tab choose, Return answers, Escape postpones) and the pointer (hovering selects, a click chooses), through `windowInputMouseEvent` on KWin before the pointer events and `pointerMotion`/`pointerButton` after them; the build probes which. Tested in `upscale-x11-prepared`: the test driver reports a window that stays large, as validation would, passes keys and clicks through KWin's input; the helper is asked, the question appears, the selection moves, Return answers, the restart offer follows, the window is asked to close and the helper to restart, Escape postpones, a click on "Never for this game" answers never, and a window is never asked twice. Only the older pointer API runs in a session test; the newer one is compiled in the Neon container. A controller reaches KWin only as the keyboard or mouse Steam Input makes of it, so the question has no controller input of its own |
| Effect: ask the helper after a refusal where the client drew another size (`x11resolution_validate.cpp`), and for each new ordinary X11 window; pin and present a prepared window fullscreen at its own size, and give it back when its user leaves fullscreen or the effect lets go (`x11resolution.cpp`, `x11resolution_prepared.cpp`, `helper.cpp`) | Done. `upscale-x11-prepared` covers the presentation and its end in a real KWin session with Xwayland. What remains untested is the trigger itself: validation finding a client that goes on drawing another size, the way Wine does, has no stand-in client yet, so the test driver reports it instead. Nothing has run in a session with a game |
| Following the settings: `present` tells the helper the size the effect wants now, for every ordinary X11 window; a prepared game whose size changed is written at the new size after that run, and one the effect no longer acts on, or whose fullscreen method is Off, is undone after that run and not presented (`preparation.cpp`, `winedesktophelper.cpp`) | Done. `followsANewSize` and `undoesWhatIsNoLongerWanted` in `upscale-wine-desktop-helper`; the session test checks the wanted size the effect sends |
| Proton outside Steam (umu, Heroic): Proton's layout, `pfx` in `STEAM_COMPAT_DATA_PATH`, is recognised and its lock taken whatever the directory is called; only Steam's own layout, the directory named after `SteamAppId`, is offered a restart | Done. `crossChecksProton` in `upscale-wine-prefix` |
| Settings page: "Prepared Games", each with Reset, shown only while a helper answers and prepared something (`preparedlist.cpp`) | Done. `upscale-prepared-list` runs it against a stand-in helper on a private session bus |
| Builds and tests, 2026-09-22 | Trixie container, GCC and Clang, warnings as errors: 26 of 26 pass, among them `upscale-x11-prepared`, which runs a real KWin session with Xwayland: a stand-in helper answers `present`, the ordinary 1920 × 1080 window is made fullscreen, held at its size, presented by the effect at 3840 × 2160, and given back at its size when its client leaves fullscreen. Neon container (KWin master), GCC and Clang: 20 of 20 pass (the X11 sessions run only against KWin before 6.7, as before). clang-tidy over `src/`: see the commit that fixed its findings |
| First session test with Wreckfest, 2026-09-23 | Ran. The question appeared, Set up and Restart game and apply were answered, the helper wrote both values into the prefix at 07:00:20 and had Steam start the game again. The second run still drew 3840 × 2160, and the effect's request failed again. Cause found in source, see below: a virtual desktop does not keep a game from choosing a larger mode. The helper then undid its own change, as designed, when the effect reported that it wanted nothing for that window |

## Why a virtual desktop does not hold every game

Found on 2026-09-23, from the session test above and Proton's Wine
(commit dc26e61):

- Explorer creates the desktop at the size in `Desktops\Default`
  (`programs/explorer/desktop.c:868-914`, `1300-1345`).
- The modes offered inside it are built from that size up to `ctx->primary`
  (`dlls/win32u/sysparams.c:2907-2971`), and `ctx->primary` is the host's
  current mode, taken from the driver before any stored mode is read
  (`sysparams.c:2339`). So every mode up to the output's own stays on offer.
- A game with a higher resolution in its own settings therefore asks for it,
  Wine grants it, and the virtual desktop is grown to it
  (`dlls/win32u/defwnd.c:3148-3170`). That is what the session test saw.

So the route holds a game that takes the current or the desktop resolution,
and not one that insists on a stored larger one. Wreckfest is the second kind.

Rejected as the way to close that gap: making KWin report its window manager
name as `steamcompmgr`, which would let Wine take the prefix's stored mode as
the host's (`sysparams.c:2338`). Proton's Wine branches on that name in about
twenty places, among them fullscreen, maximise, minimise, focus and activation
(`dlls/winex11.drv/window.c:1005`, `1445`, `1554`, `1878`, `1922`, `3718`,
`3756`, `event.c:329`, `686`, `937`, `dlls/win32u/window.c:4823`, `5954`,
`defwnd.c:261`, `514`, `1896`, `sysparams.c:3044`, `4625`). Every Proton
program in the session would take gamescope's paths. Jens rejected it on
2026-09-23, with the launch-time routes (gamescope or a private Xwayland per
game, whether through Steam's launch options or a compatibility tool) as too
invasive. A solution has to stay least invasive and as generic as possible.

## What was searched for instead, and found wanting

Asked on 2026-09-23 for the least invasive and most generic lever that would
hold a game which keeps a larger resolution in its own settings, under the
rules that only the effect and its companion act, that the game's start command
and its own configuration stay untouched, and that no other program notices
anything. The answer, from the sources:

- **Nothing generic exists.** Wine decides every size either from the host
  output (the driver's current mode, `sysparams.c:2339`) or, in Xwayland, per
  requesting client; a third party can reach neither.
- **One brittle candidate**, recorded for the knowledge and not implemented:
  Wine trusts a display-device cache in the prefix's registry when it is
  present and complete (`sysparams.c:719-786`, `2731-2820`, `3061`), and keys
  that come from `system.reg` are not volatile, so a companion could fabricate
  `HARDWARE\DEVICEMAP\VIDEO` and `Control\Video\{guid}\Sources\...` with a
  mode list that has no 4K, plus `EmulateModeset` for that one executable
  (`sysparams.c:6183-6204`). The game's first mode request would then be the
  wanted size, Wine would make it a real RandR request, and Xwayland's
  per-client emulation would hold it for the rest of the session
  (`hw/xwayland/xwayland-output.c:1040-1094`). Against it: the virtual desktop
  must be off, Wine overwrites the cache on the first mode change and explorer
  saves its own view when the game exits, so the companion would have to
  fabricate Wine's internal display state again after every session; two links
  are unverified (the refresh rate in `is_same_devmode`,
  `winex11.drv/display.c:178-185`, and KWin 6.3.6, which has no code for
  Xwayland's emulation property while master has). Fabricating Wine's internal
  display cache in a user's prefix is the opposite of least invasive.
- **What would make it generic**, and both are elsewhere: Xwayland letting a
  window manager set an emulated mode for another client, or Proton honouring a
  size the window manager imposes while a present rectangle is set.

So the route keeps the games it holds, and for the others the effect says
plainly that the game keeps a resolution of its own. A prefix that was prepared
and did not help is undone again by the helper and not offered a second time.

## Decisions and open questions

- Accepted by Jens, 2026-09-22: the route itself, with the user's consent
  before any write and an offer to restart the game. The handbook's fourth
  requirement and its ruling on "the private virtual desktop" have to be
  updated to say so: the consent is what allows a change that outlives
  uninstalling, and this form works through configuration, not at start.
- Decided 2026-09-22, by the plugin-folder rule: the plugin defines a generic,
  optional interface, `org.kde.KWin.Upscale.Helper1`
  (`src/plugins/upscale/org.kde.KWin.Upscale.Helper1.xml`): "a program does
  not render at the size wanted; can you prepare it?" and "did you prepare this
  window's program?". It knows nothing about Wine, and without a helper it
  behaves as before. The companion lives in `src/winedesktop/`, outside the
  plugin, and writes every text the user reads about it.
- Open: the companion's texts use their own translation domain,
  `kwin_upscale_helper`, which the translation extraction does not cover yet.

## Sources

Read locally, not vendored: Proton's Wine at commit dc26e61 (Proton 11),
the proton script of Proton Experimental 11.0-20260917b, DXVK 3.1.1,
Xwayland 24.1.6, KWin 6.3.6, steam-runtime-tools v0.20260805.0 and
plasma-workspace 6.3.6.
