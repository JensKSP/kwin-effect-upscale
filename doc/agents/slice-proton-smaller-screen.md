<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Wine and Proton games through a smaller screen in their prefix

## Status

Written down on Jens's instruction on 2026-09-22 and accepted by him the same
day as the route for Wine and Proton games that ignore resizing, on the
condition that the user is asked first and offered a restart. The first form of
it, a Wine virtual desktop, ran in a session with Wreckfest on 2026-09-23:
everything the effect and the companion do worked, and the game still drew
3840 × 2160, because a virtual desktop does not keep a game from choosing a
larger mode; see
[why a virtual desktop did not hold every game](#why-a-virtual-desktop-did-not-hold-every-game).

Jens refused to leave it there and asked for an automatic route at Wine level
that holds most games with the least change. That route was found, measured and
implemented on 2026-09-23: the companion describes a smaller screen in the
game's own prefix, where Wine reads its display from before it asks the display
server, so the game's own mode choice can reach no further than that screen. See
[the screen in the prefix](#the-screen-in-the-prefix) for the mechanism and
[the experiment that ran](#the-experiment-that-ran-2026-09-23) for the
measurement. What remains open is a session test with a game.

The citations name the upstream file and line in the versions listed under
[sources](#sources).

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

A screen of our own in the game's own prefix makes Wine report that size as the
monitor and offer no larger mode. Proton gives every Steam game its own prefix
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
3. **A companion describes a W × H screen** in that game's `system.reg`, only
   while no Wine server runs for the prefix; see
   [the screen in the prefix](#the-screen-in-the-prefix).
4. **The effect offers to restart the game** in the same centred display, so
   the change takes effect at once instead of at the next start.
5. **From the next start** every process in the prefix sees a W × H monitor and
   a mode list that reaches no further, so a game that takes the current mode
   renders at W × H and a game that asks for a larger one is given the closest
   the list holds (DXVK `src/dxgi/dxgi_output.cpp:588-603`, called from
   `src/dxgi/dxgi_swapchain.cpp:870`). The mode change stays inside the prefix
   (`sysparams.c:4623`), so the X screen never changes.
6. **The effect enlarges the game's window**: it makes it fullscreen in KWin
   while holding its X size at W × H, the sequence its X11 path already uses
   (`x11resolution_events.cpp`), upscales with FSR 1 and maps input.

Nothing in Proton resets the description. The proton script edits `system.reg`
only by the lines it names (xinput, DDE, shared GPU resources), `wine.inf` has
no video or device-map keys, and a downgrade of Proton, `destroyprefix` or a new
prefix removes the description altogether. The companion must therefore check
again at every start, never assume.

## The screen in the prefix

Wine keeps its own description of the display inside the prefix and reads it
before it asks the display server. Found on 2026-09-23 in Proton's Wine
(commit dc26e61):

- `lock_display_devices` calls the driver only when reading the description
  fails (`sysparams.c:3061-3068`); a complete description means the display
  server is never asked, in any process of that prefix, for the whole session.
- The description is read from `HKLM\HARDWARE\DEVICEMAP\VIDEO` and the key it
  names below `System\CurrentControlSet\Hardware Profiles\Current`
  (`read_source_from_registry`, `sysparams.c:719-786`,
  `update_display_cache_from_registry`, `2731-2820`): the state flags, the dots
  per inch, the mode list with its count, the current and the registry mode, the
  graphics card and the monitor.
- Wine itself creates that key as a **volatile symbolic link** to the values it
  writes for the session (`write_source_to_registry`, `sysparams.c:1930-1950`).
  A real, non-volatile key of ours in its place is read instead, and keys loaded
  from a registry file are never volatile, so a later volatile create does not
  change ours (`server/registry.c:700-727`).
- The mode list is what decides the game's choice: Wine refuses a mode that is
  not in it (`find_display_mode`, `source_get_full_mode`, `sysparams.c:4251-4305`,
  `4674`), and the list Wine would build itself reaches the host's own mode
  whatever the prefix says (`2339`, `2909`). Ours holds one size, the chosen one,
  in the three colour depths Wine offers and at 60 Hz plus the output's own rate.
  A list entry's rate of zero would match any request; the honest rates are
  written instead.
- One size rather than a capped list, decided after the session test of
  2026-09-23: Wreckfest read a capped list, discarded the 3840 x 2160 in its own
  settings, and came up with its resolution question and the smallest offered
  size preselected. Which size a game picks from a list is the game's own
  business; the size it is to render at is the effect's. With one size there is
  nothing to pick.
- The current mode is written without a rate, as Wine writes it, and no
  `Physical` value is written, so the raw and the virtual monitor rectangle
  coincide (`sysparams.c:773`, `monitor_get_rect`, `2573-2596`) and no DPI
  scaling stands between the game and its window.
- What the mode change itself does: with Proton's default `emulate_modeset`
  (`sysparams.c:170`) `apply_display_settings` reports success without calling
  the driver (`4623`), writes the new mode into our key (`4630-4636`) and forces
  a refresh, which reads our key again (`3068`). Wine's own values go to the
  volatile key nobody reads. So the description survives a mode change, and the
  server saves it with the file, so it survives the session.
- The graphics card and the monitor are not fabricated: the prefix describes
  them itself, in keys below `System\ControlSet001\Enum` that outlast a session
  and exist once a program of its own has run. Without them the companion
  refuses to write.

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
- Change only the two keys and leave every other byte alone. The keys are
  written whole, so a description Wine added to since is replaced with the
  values it should have, and removing it removes what Wine added as well.
- Never prepare a prefix whose programs run in a virtual desktop the user set
  (winecfg, protontricks): there explorer's desktop and our screen would
  describe different sizes. The companion keeps its own record of the prefixes
  it changed, in `$XDG_STATE_HOME`, and undoes only its own change. Reset remains
  available when the user enables a virtual desktop after preparation and
  leaves the user's desktop settings unchanged.
- Undo when the user resets it on the settings page: at once for a game that
  is not running, after exit for one that is. Follow the settings when the
  game's window next appears: the effect tells the helper the size and the rate
  it wants now, and a changed size is written, or the description taken away
  when the effect no longer acts on the game, after that run.
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
  every flavour not yet verified. A game that renders at a size of its own
  whatever its screen offers is outside this scope, and the helper takes its own
  preparation back for it.
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
- A prefix that has not described its own devices yet, or whose programs run in a
  virtual desktop of the user's, is refused rather than written.

## The experiment that ran, 2026-09-23

In a prefix of its own under `build/`, with Proton Experimental 11.0-20260917b's
Wine, on a 3840 × 2160 X server of its own. No game, no prefix of Jens's and no
session of his was touched.

1. A fresh prefix on that X server, then a Windows program that prints
   `GetSystemMetrics` and `EnumDisplaySettings` and opens a window of the screen's
   size. It reported `screen=3840x2160 current=3840x2160`.
2. With no Wine server running, the two keys were added to `system.reg` for
   2560 × 1440 — once from Wine's own values, shrunk, and once generated from
   nothing but the prefix's `Enum` keys, which is what the companion does.
3. The same program then reported `screen=2560x1440 current=2560x1440`, and its
   fullscreen window was a **2560 × 1440 X11 window at 0,0 on the 3840 × 2160
   X screen**, which is the shape the effect pins and presents.
4. A mode change to 1920 × 1080 from inside the program returned success, moved
   the screen and its window to 1920 × 1080, and the X screen stayed 3840 × 2160.
5. After that change the description was still the one written: Wine had added
   only `Depth` and a stray `SymbolicLinkValue` to the key.
6. The prefix's server was killed and the program run again from the file on
   disk: `screen=2560x1440`. No re-seeding was needed.

What this does not show, and the session test with a game has to: that a game
whose own settings name a larger resolution is snapped down to the list rather
than refusing to start, and that KWin and the effect present the game's own
window as they present the test client's.

## Progress

### Build repair and pull request review, 2026-09-23

PR #21 at `04fb2f2` failed before tests in every compiler job: the helper
split left the job's private functions and timing constants in the other
translation unit, omitted its prefix declarations, and defined the logging
category twice. The private dependencies now live with the job implementation;
the helper owns the one logging definition and the job declares it. Static
analysis then found two naming issues and excessive complexity in `advance()`;
the names now follow the configured rules and the write step has its own
function, with behavior unchanged.

Observed on the repaired working tree, in the maintained containers:

- Trixie GCC and Clang: warnings as errors, all 26 runtime tests passed with
  each compiler, and both staged installs passed.
- Neon GCC and Clang: all targets built with warnings as errors. Neon runtime
  tests were not run; this is the build-only KWin master compatibility check.
- Trixie coverage: all 26 runtime tests passed; plugin line coverage is 92.5%
  (4725 of 5110 lines), above the 90% gate.
- Trixie address/undefined-behavior sanitizers and ThreadSanitizer: all 26
  runtime tests passed in each run. The 60-second fuzzing check also passed.
- Both pre-commit stages passed, including tooling regression tests.
  clang-tidy passed after the full source scan's findings were corrected and
  the affected helper files were rechecked. Plugin metadata validation passed.

The changes are local and uncommitted. PR #21 still points to `04fb2f2`, with
the old failed CI checks and pending CodeRabbit approval. Hosted CI, arm64 and
package builds have not been rerun.

The review reproduced three outstanding correctness findings with isolated
probes under `build/`, linked against the Trixie helper library:

- `holdsWhatWasWritten()` compares the requested screen list with the stored
  one without allowing the writer's normalization. Both two requested screens
  with only one known monitor and a requested refresh rate of zero cause
  repeated `WrittenMeanwhile` retries after a successful write. Neither job
  finishes or reaches its requested restart; the busy deadline is twelve hours.
- A user who enables a Wine virtual desktop after preparation cannot reset
  the companion's screen description: the shared registry read rejects the
  clear with `DesktopOfTheUser`. The keys in `system.reg` remain.
- Losing the prefix's screen description and then calling `present()` followed
  by `offer()` marks the game `never` even though it has not run with the
  preparation. The existing rewrite job prevents the requested undo from
  replacing it.

### Preparation repair before hardware acceptance, 2026-09-23

The three findings above are now addressed in the working tree:

- Read-back verification belongs to the registry writer, against the text it
  actually produced. Capping the monitor count and substituting a refresh rate
  are successful writes, not concurrent changes. The helper's restart test now
  covers both normalizations and verifies that the following presentation does
  not queue another write.
- A user's virtual desktop prevents a new preparation, but not removal of an
  earlier one. A refused update retains its preparation record so Reset remains
  available. Writer and helper tests check removal and unchanged `user.reg`.
- An offer waits for a pending repair and checks the prefix itself before
  calling a prepared game unsupported. A lost description is restored after
  the run, whether or not `present()` was called before `offer()`.

The helper fixture and the companion's test build declarations have their own
files to keep the expanded tests below the code-line limit. The focused Trixie
GCC writer and helper tests passed. Both pre-commit stages passed before the
remaining review cleanups. All 26 GCC runtime tests then passed, but the hook
reported a concurrent source change and stopped before the staged install;
the finishing checks were repeated against the settled tree. Both hook stages
passed again, Trixie GCC and Clang each passed all 26 runtime tests and the
staged install, and Neon GCC and Clang built all targets with warnings as
errors. Neon runtime tests were not run. The full production clang-tidy scan
and plugin metadata validation passed. Coverage and sanitizer results above
precede these behavior changes; hosted checks will rerun them after the push.

Other valid review cleanups restore the X11 test's previous focus policy even
on an early assertion failure and clarify the README's package requirements
and per-texture memory estimate. Two review suggestions conflict with current
requirements: the pointer API fallback supports copying the plugin into KWin
and is not generated build identity, while durable design belongs in the
handbook under the current documentation rules. The older concurrent registry
writer finding remains open: the Proton lock and server checks do not establish
exclusion against every possible registry writer. Wine 11's `server/request.c`
confirms that taking the server's lock can cause a concurrent launch to exit,
as the existing design notes explain; the current recovery checks do not
prove preservation of an unrelated edit made between the read and rename.

Jens's next hardware acceptance sequence is Wreckfest, Extreme Tux Racer,
SuperTuxKart and Left 4 Dead 2 on wzpc. Final review and merge follow only if
those results and the PR checks are satisfactory. Further listed OSS games
and Flatpak/Snap experiments follow that baseline; they are not acceptance
claims for this candidate.

### Implementation and acceptance

| Step | State |
| --- | --- |
| Registry edit: describe and undescribe the screen in `system.reg` text, leaving every other byte, and read the user's virtual desktop out of `user.reg` (`src/winescreen/wineregistry.cpp`); the binary modes win32u reads (`winedisplaymode.cpp`) | Done. `upscale-wine-registry` and `upscale-wine-display-mode` pass in both containers with GCC and Clang, warnings as errors |
| Server lock check (`F_GETLK` on the prefix's lock, and the holder's process so the wait outlives the game's view of the file system) (`wineserverlock.cpp`) | Done |
| Locating the prefix from the game's process, with every check above (`wineprefix.cpp`; Linux `/proc` in `wineprocess_proc.cpp`, nothing elsewhere) | Done |
| Describing and undescribing the screen after the game exited: proven directory only, under Proton's `pfx.lock`, atomic, a prefix with the user's own virtual desktop left alone (`winescreenwrite.cpp`) | Done. `upscale-wine-prefix` and `upscale-wine-desktop-write` pass in the Trixie container with GCC and Clang, warnings as errors; Neon not run |
| Companion service `kwin-upscale-helper`: D-Bus activated as `org.kde.KWin.Upscale.Helper`, implementing the plugin's optional `org.kde.KWin.Upscale.Helper1` interface; its record of prepared prefixes in the user's state directory; waiting for the game and its server, writing, restarting through Steam, presenting, reset and "never" (`winescreenhelper.cpp`, `winescreenservice.cpp`) | Done. `upscale-wine-desktop-helper` runs it against real processes and a held server lock; all four companion tests pass 20 times in a row in the Trixie container with GCC and Clang. The service, its D-Bus activation file and its user unit install; the RPM recipe lists them. Neon not run |
| Centred display: question and restart offer (`question.cpp`, `preparation.cpp`; the selected answer in Breeze's highlight colour) | Done. It holds the keyboard (arrow keys and Tab choose, Return answers, Escape postpones) and the pointer (hovering selects, a click chooses), through `windowInputMouseEvent` on KWin before the pointer events and `pointerMotion`/`pointerButton` after them; the build probes which. Tested in `upscale-x11-prepared`: the test driver reports a window that stays large, as validation would, passes keys and clicks through KWin's input; the helper is asked, the question appears, the selection moves, Return answers, the restart offer follows, the window is asked to close and the helper to restart, Escape postpones, a click on "Never for this game" answers never, and a window is never asked twice. Only the older pointer API runs in a session test; the newer one is compiled in the Neon container. A controller reaches KWin only as the keyboard or mouse Steam Input makes of it, so the question has no controller input of its own |
| Effect: ask the helper after a refusal where the client drew another size (`x11resolution_validate.cpp`), and for each new ordinary X11 window; pin and present a prepared window fullscreen at its own size, and give it back when its user leaves fullscreen or the effect lets go (`x11resolution.cpp`, `x11resolution_prepared.cpp`, `helper.cpp`) | Done. `upscale-x11-prepared` covers the presentation and its end in a real KWin session with Xwayland. What remains untested is the trigger itself: validation finding a client that goes on drawing another size, the way Wine does, has no stand-in client yet, so the test driver reports it instead. Nothing has run in a session with a game |
| Following the settings: `present` tells the helper the size the effect wants now, for every ordinary X11 window; a prepared game whose size changed is written at the new size after that run, and one the effect no longer acts on, or whose fullscreen method is Off, is undone after that run and not presented (`preparation.cpp`, `winescreenhelper.cpp`) | Done. `followsANewSize` and `undoesWhatIsNoLongerWanted` in `upscale-wine-desktop-helper`; the session test checks the wanted size the effect sends |
| Proton outside Steam (umu, Heroic): Proton's layout, `pfx` in `STEAM_COMPAT_DATA_PATH`, is recognised and its lock taken whatever the directory is called; only Steam's own layout, the directory named after `SteamAppId`, is offered a restart | Done. `crossChecksProton` in `upscale-wine-prefix` |
| Settings page: "Prepared Games", each with Reset, shown only while a helper answers and prepared something (`preparedlist.cpp`) | Done. `upscale-prepared-list` runs it against a stand-in helper on a private session bus |
| The route changed from a virtual desktop to a described screen, 2026-09-23 | Done. The helper's write target and the effect's message changed; the helper is now told the output's refresh rate as well, since the modes it describes carry one. Everything else — consent, proof of the prefix, restart, records, undo, "never", the settings page — is unchanged. Measured in a prefix of its own, see [the experiment that ran](#the-experiment-that-ran-2026-09-23) |
| Builds and tests, 2026-09-22 | Trixie container, GCC and Clang, warnings as errors: 26 of 26 pass, among them `upscale-x11-prepared`, which runs a real KWin session with Xwayland: a stand-in helper answers `present`, the ordinary 1920 × 1080 window is made fullscreen, held at its size, presented by the effect at 3840 × 2160, and given back at its size when its client leaves fullscreen. Neon container (KWin master), GCC and Clang: 20 of 20 pass (the X11 sessions run only against KWin before 6.7, as before). clang-tidy over `src/`: see the commit that fixed its findings |
| Builds and tests of the changed route, 2026-09-23 | Trixie container, GCC: 27 of 27 pass, the FSR regression tests and the install included. Neon container (KWin master), GCC: 21 of 21 pass. Clang in both, clang-tidy, the lint stages and coverage: see the commit |
| Session test with Wreckfest on the described screen, 2026-09-23 | Ran. The game renders at the size the prefix describes, the effect presents and upscales it, and the frame rate is what the smaller buffer gives. Two things came out of it: a capped list let the game throw away its stored 4K and preselect the smallest offered size, so the prefix now describes one size only; and the pointer over the presented window belonged to whatever lay under it, which the effect's input filter now claims. Jens saw both. Outstanding from this run: the prefix's own resolution list holds one entry while a game is prepared, which is deliberate, and the game asks once about its resolution when its stored one is gone |
| Mouse look over a presented X11 window, 2026-09-23 | Done. Xwayland asks to lock the pointer for the window that holds the seat's pointer focus, which the filter gives it; KWin takes that lock only while its own focus is on the client's own rectangle. The effect now puts the cursor inside that rectangle once, when a presented window has asked for a lock KWin has not taken and is the active window. `letsAPresentedGameLockThePointer` in `upscale-x11-prepared` has the client hide its cursor and grab the pointer from outside its own rectangle: the lock reads "asked" without this and "engaged" with it. A confinement's region stays the client's own, unscaled, and is still not mapped |
| The pointer over a presented X11 window, 2026-09-23 | Done. KWin's hit test goes through the client's input region, so beyond the game's own window it found the desktop: the pointer went there and a click raised it in front of the game. The filter now focuses the presented surface wherever the effect presents it, unless KWin found a window stacked above, and delivers the click and the wheel itself ahead of KWin's own click handling, which would otherwise act on the window underneath. `keepsThePointerOverWhatItPresents` in `upscale-x11-prepared` puts a window under a presented one and checks that the pointer arrives in the game's coordinates, that the click is the game's and that the window underneath gets none; `upscale-x11-integration` checks the mapping beyond the client's own window as well. The window underneath still sees the pointer enter it, which no filter can prevent |
| First session test with Wreckfest, 2026-09-23 | Ran with the virtual desktop. The question appeared, Set up and Restart game and apply were answered, the helper wrote both values into the prefix at 07:00:20 and had Steam start the game again. The second run still drew 3840 × 2160, and the effect's request failed again. Cause found in source, see below: a virtual desktop does not keep a game from choosing a larger mode. The helper then undid its own change, as designed, when the effect reported that it wanted nothing for that window |

## Why a virtual desktop did not hold every game

Found on 2026-09-23, from the session test above and Proton's Wine
(commit dc26e61). This is why the route now describes a screen instead of a
desktop:

- Explorer creates the desktop at the size in `Desktops\Default`
  (`programs/explorer/desktop.c:868-914`, `1300-1345`).
- The modes offered inside it are built from that size up to `ctx->primary`
  (`dlls/win32u/sysparams.c:2907-2971`), and `ctx->primary` is the host's
  current mode, taken from the driver before any stored mode is read
  (`sysparams.c:2339`). So every mode up to the output's own stays on offer.
- A game with a higher resolution in its own settings therefore asks for it,
  Wine grants it, and the virtual desktop is grown to it
  (`dlls/win32u/defwnd.c:3148-3170`). That is what the session test saw.

So a virtual desktop holds a game that takes the current or the desktop
resolution, and not one that insists on a stored larger one. Wreckfest is the
second kind. A described screen caps the mode list itself, which is the one
thing the desktop could not do, and holds both kinds; the desktop is not used
any more.

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

## What else was searched for, and found wanting

Asked on 2026-09-23 for the least invasive and most generic lever that would
hold a game which keeps a larger resolution in its own settings, under the rules
that only the effect and its companion act, that the game's start command and its
own configuration stay untouched, and that no other program notices anything.
What the sources gave, besides the screen in the prefix:

- **Making KWin call itself `steamcompmgr`**, which would let Wine take the
  prefix's stored mode as the host's (`sysparams.c:2338`): rejected. Proton's
  Wine branches on that name in about twenty places, among them fullscreen,
  maximise, minimise, focus and activation
  (`dlls/winex11.drv/window.c:1005`, `1445`, `1554`, `1878`, `1922`, `3718`,
  `3756`, `event.c:329`, `686`, `937`, `dlls/win32u/window.c:4823`, `5954`,
  `defwnd.c:261`, `514`, `1896`, `sysparams.c:3044`, `4625`). Every Proton
  program in the session would take gamescope's paths. Jens rejected it with the
  launch-time routes (gamescope or a private Xwayland per game, through Steam's
  launch options or a compatibility tool) as too invasive.
- **A real RandR request per program**, by switching `EmulateModeset` off for one
  executable (`sysparams.c:6183-6204`) so Xwayland's per-client emulation holds
  the mode (`hw/xwayland/xwayland-output.c:1040-1094`): it needs the described
  screen anyway, since without it the game asks for the output's own mode, and
  then its result depends on KWin's handling of Xwayland's emulation, which
  6.3.6 has no code for. More parts for the same outcome; not taken.
- **A Vulkan layer** the package installs (pressure-vessel imports host layers):
  it can shrink what is presented, never what the game renders into, because the
  render targets follow the mode the game chose. Dropped.
- **DXVK configuration**: the only places DXVK reads are an environment variable
  and the game's own directory (`src/util/config/config.cpp:1751-1760`), both
  excluded, and no DXVK option caps a resolution anyway.
- **`PROTON_LIMIT_RESOLUTIONS`** (`sysparams.c:6207-6213`): an environment
  variable, so it needs the start command; and it truncates the list after the
  largest mode was inserted first (`2199`), so the output's own mode stays.
- **What would make even the hard cases generic**, and both are elsewhere:
  Xwayland letting a window manager set an emulated mode for another client, or
  Proton honouring a size the window manager imposes while a present rectangle is
  set.

So a game that renders at a size of its own whatever the screen offers is
still beyond the route, and for those the effect says plainly that the game keeps
a resolution of its own. A prefix that was prepared and did not help is undone
again by the helper and not offered a second time.

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
  behaves as before. The companion lives in `src/winescreen/`, outside the
  plugin, and writes every text the user reads about it.
- Decided 2026-09-23, after the session test: a described screen instead of a
  virtual desktop, because only the description caps the modes a game may choose
  from. The consent, the proof of the prefix, the restart and the undo are the
  same; the interface gained the output's refresh rate, since a described mode
  carries one.
- Open: the companion's texts use their own translation domain,
  `kwin_upscale_helper`, which the translation extraction does not cover yet.
- Decided 2026-09-23: every output of the session is described, the game's own
  first and at the wanted size, so that a program sees the screens the session
  has. There are never more screens than monitors the prefix knows, because Wine
  gives a screen without one no size. The record keeps the whole layout in one
  line, so another rate, another size or a screen plugged or unplugged has the
  prefix described again after the next run.
- Decided 2026-09-23, on Jens's instruction: an untested Wine build is written
  for and named in the log rather than refused, since refusing would take the
  feature from every build released after this one. What the log names is
  Proton's own version file beside the prefix; plain Wine says nothing, and that
  is logged as well.
- Decided 2026-09-23: the run the helper started itself is watched, so a game
  that will not start with the screen it was described - the one case the
  preparation cannot be judged by what the game draws - has the description taken
  back and is not offered again.
- Decided 2026-09-23: the record keeps the rate the screen was described for, so
  an output switched to another rate has its prefix described again after the next
  run, the same way a changed size does (`followsANewRate`).

## Sources

Read locally, not vendored: Proton's Wine at commit dc26e61 (Proton 11),
the proton script of Proton Experimental 11.0-20260917b, DXVK 3.1.1,
Xwayland 24.1.6, KWin 6.3.6, steam-runtime-tools v0.20260805.0 and
plasma-workspace 6.3.6. The experiment ran against the same Proton
Experimental's Wine, on Xvfb, in a prefix under `build/`.

### Queued repair follows current settings, 2026-09-23

Review of `94a6804` found that a lost-screen repair queued by `offer()`
survives a later empty `present()` request. The game can therefore be
prepared after the user selects Off. Update automatic after-run jobs from
the latest presentation request, including clearing or changing the size.
Keep explicit Reset and accepted preparation jobs distinct from automatic
maintenance, and preserve an unsupported game's queued removal. Planned
regressions cover lost preparation followed by Off or another size and an
explicit Reset while the game remains open. Validation is pending.

On wzpc, the user-authorized clean-start reset removed Wreckfest's preparation
through the helper and temporarily removed personal upscaler overrides.
Backups are retained in `build/wzpc-clean-start-94a6804/`. The original global
`OsdStatistics=true` override must be restored after this test, preserving any
new choices Jens makes. The earlier Wreckfest run had active upscaling and
accurate edge input but used existing preparation; clean-start acceptance
is still outstanding.

The queued-maintenance fix passed the complete helper regression in Trixie,
including lost preparation followed by Off or another size, and explicit
Reset while the running game keeps presenting its preference. It is included
in the native diagnostic installation; hosted checks and hardware acceptance
of this new revision remain outstanding.
