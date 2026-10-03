<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Wine and Proton games at a smaller screen

## Status

**Status on 2026-09-29**, above the record of how it got here: the companion that
prepared a prefix's screen, which the paragraphs below describe, left the
default build and the packages on 2026-09-24 and the source tree on 2026-09-29
(item 32): its service, the effect's helper client, preparation, question and
prepared-window path, the settings page's Prepared Games and its tests. What
stays of it is the guard: a Wine window whose connection the proxy did not
answer is neither held at its first mapping nor resized, and the status now
names that reason; `leavesWineToTheProxy` checks it. The route now is the session X11
proxy: the prefix is the unit, a profile matches the tail of the program's
path through `wine://<prefix>/<path>`, a Wine component is held until its
prefix's program is known (ten seconds at most), and each decision logs the
names it was matched against. No shipped profile names a Wine program yet;
Wreckfest's needs the path Proton reports, taken with the run on wzpc (item 30).
Open, as the open list numbers them: acceptance on real games (29 to 31, 34 to
36), Wine's Wayland driver (35), and warm prefixes (27).

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

On 2026-09-25 Jens settled a different route for the same topic: the X11
forwarding proxy gives every connection of a game's prefix a smaller screen,
and the prefix companion stays retired. This slice keeps the topic and changes
the mechanism; see
[the proxy replaces the prefix screen](#wine-identity-through-the-proxy-replaces-the-prefix-screen-2026-09-25).
The measurements below stand, and the reason a virtual desktop did not hold
every game is why a prefix still has exactly one screen size to give.

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

Replaced by Jens's decision of 2026-09-29 (item 25 of the open list): the gates
below are the proxy route's, which replaced the prefix companion on 2026-09-25.
The companion's gates and acceptance criteria are in Git history.

- **Supported scope:** Wreckfest (DXVK D3D11, exclusive fullscreen) with Proton
  Experimental on Debian Trixie's KWin 6.3.6 through its shipped entry, and one
  game under Wine outside Steam. Each is launched normally, with no question,
  no restart and no write to its prefix, and supplies a W × H buffer upscaled
  across the output, with input working, a confined pointer included (item
  29a); the status names the program, and switching its slot Off in the settings
  gives it the full size at its next start.
- **Full acceptance:** every flavour listed under the
  [additions](#additions-laid-down-by-jens-2026-09-22), Flatpak and Snap
  installs of Steam and Wine's Wayland driver included, across the handbook's
  matrix (D3D9, D3D11, D3D12, OpenGL, Vulkan; exclusive and borderless) on real
  hardware, with automated tests for the program's identity (Unix paths, drive
  mapping, the prefix's hold), its presentation and its input mapping.

## Acceptance criteria

- A Wine or Proton program is identified by the program path Wine names, from
  a Windows path or a Unix one, on the prefix's drives, each shape covered by a
  test.
- A prefix's connections wait for its program at most ten seconds, and one
  whose program matches no entry is answered unchanged.
- The program an entry names is told the smaller screen at its connection,
  renders at it, and is presented over the whole output with its pointer
  mapped; one it does not name keeps the screen as it is.
- Switching the entry's slot Off, or the effect off, gives the next start the
  full screen; nothing is written to a prefix at any point.

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
SuperTuxKart and Left 4 Dead 2 on wzpc. **Run 2026-10-03 on the television**
(KWin 6.3.6, 3840 × 2160 at scale 3, Quality): all four were offered
2560 × 1440 by the session proxy or advertised it on Wayland, and the
effect enlarged each. Jens played Wreckfest, after item 98 of the open list,
Extreme Tux Racer and Left 4 Dead 2 and found picture and pointer right;
SuperTuxKart ran in all six presentations in the automated check of item 71. Final review and merge follow only if
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
  Done, and followed since: the handbook states this exception beside the
  fourth requirement, notes that the helper is out of the default build, and
  describes the proxy route that replaced it, which writes nothing and needs no
  consent (checked 2026-09-28).
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
  Moot since the companion left the default build and the packages
  (`UPSCALE_BUILD_WINE_HELPER` off): nothing of it is installed to translate. It
  returns only if the companion is kept, which is item 32 of the open list.
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

On 2026-09-24 Jens requested clearing Wreckfest's Wine preparation before proxy
testing. Inspection found no forced screen sections in system.reg and neither
an Explorer Desktop value nor a Default virtual-desktop size in user.reg. The
helper still had one Wreckfest record with Described=2560x1440+0+0@120 and
Never=true. Backed up both registries and the helper record in
build/wreckfest-wine-cleanup-e4rtqbqw, then removed only that Wreckfest record.
Read-back verified the record removal and byte-for-byte unchanged registries.
No game settings, saves, prefix recreation or game launch was involved. This
establishes removal of preparation state, not successful proxy game acceptance.

The queued-maintenance fix passed the complete helper regression in Trixie,
including lost preparation followed by Off or another size, and explicit
Reset while the running game keeps presenting its preference. It is included
in the native diagnostic installation; hosted checks and hardware acceptance
of this new revision remain outstanding.

### Leave unprepared Wine runs alone, 2026-09-24

Jens observes Wreckfest flickering and jumping while Auto tries resizing before
offering preparation. He requests recognizing Wine/Proton first, leaving the
running game untouched until it restarts prepared, and then taking PR #21
through checks and review. The saved ETR/STK checkpoint is bb5aef6.

Detect Wine's standard loaders and preloaders through KWin's portable process
identity API, before the mapping barrier or any resize request. This is runtime
classification, independent of the game profile and prefix-write authorization.
Ask the existing helper about such windows even when they start fullscreen.
An unprepared answer may offer setup directly; a prepared answer must still
wait for the actual smaller buffer before changing presentation. A saved record
or an accepted setup question must never permit resizing the current native-size
run. Missing helper replies leave recognized Wine windows alone. Native X11
startup, particularly ETR's early mapping transaction, must remain unchanged.

Supported acceptance: container regressions for fullscreen and later-fullscreen
Wine identities, no resize while setup/restart is pending or a helper is absent,
and presentation only after a prepared buffer arrives; existing native startup
and helper regressions, both maintained containers/compilers, lint and static
analysis. Full acceptance adds Wreckfest's clean preparation/restart run on wzpc.
No per-game config edits, launcher requirements or Wine-prefix writes without
the existing consent flow are introduced. Renamed/custom loader executables are
outside this initial recognition rule; prefix validation remains the helper's.

The first four runtime-gate rows and the native startup/input suite pass in
Trixie. An added stale-reply row correctly sends an empty wanted-screen list
after disabling; its first assertion incorrectly compared zero size with Qt's
invalid default size and was corrected to test emptiness. During review of the
new call path, an empty present() answer was found insufficient to establish a
rendering failure. A separate offerSetup D-Bus query now distinguishes early
setup from validation failure, preventing the helper from marking an existing
preparation unsupported before it has been observed. A helper regression keeps
the written record and verifies no removal job is queued by this query.

The completed guard builds with GCC and Clang, warnings as errors, in Trixie
and Neon unstable. Both lint stages, metadata validation and static analysis
of the changed production units pass. The final Clang rendering hook passes
all 26 enabled suites; the final GCC prepared-window, helper and resolution
suites pass, following an earlier complete GCC pass. The final prepared-window
suite includes all five runtime-gate rows. An earlier hook reported source
changes made while it ran; rerunning against frozen sources passes.

The native effect, settings module and matching helper are installed on wzpc.
The helper was inactive before replacement; the new service exports offerSetup,
and installed effect/helper hashes match their native build outputs. The effect
reload reports build time 2026-09-24T09:43:20Z without a desktop restart. The
clean Wreckfest preparation/restart test, hosted checks and current-head review
remain outstanding. L4D2 coordinate tracing follows this saved checkpoint.

Current-head review of cd0b993 finds that the early setup question incorrectly
claims an observed resize failure. A separate neutral setup question now asks
about the next start; the post-failure wording remains specific to that path.
A further local regression disables the profile during the offerSetup reply:
it fails before the correction because the old offer survives and no updated
helper query follows. The callback now discards that offer, answers Later, and
refreshes the helper with the current preference. Validation results follow below.

The review also requests moving the resolution-control slice directly under
doc/. That conflicts with the current root AGENTS.md, doc/AGENTS.md and
doc/agents/AGENTS.md, which require temporary slice records under doc/agents/.
The record stays in the required location; a reply citing these rules is
prepared and awaits authorization to post.

The delayed-offer regression passes with the correction. The first broader
callback check also rejected the existing post-failure dialog fixture; limiting
this new check to early offers restores all three existing dialog cases. Final
validation passes both lint stages, GCC and Clang builds in Trixie and Neon
unstable, all 26 enabled Clang suites, the focused GCC preparation suite and
changed-production static analysis. The helper suite also passes after the
wording correction. No metadata changed from the earlier validated checkpoint.
The effect and helper are installed together; hashes match their native outputs,
and the reloaded effect reports build time 2026-09-24T10:06:29Z. The session is
still locked, so neither Wreckfest acceptance nor the L4D2 live trace has run.

### Wine identity through the proxy replaces the prefix screen, 2026-09-25

Jens settled the route on 2026-09-25: Wine and Proton games are served by the
X11 forwarding proxy that [resolution control](slice-resolution-control.md)
owns, and the retired prefix companion stays retired. The mechanism this slice
was written for, a smaller screen described in the game's own prefix, is
superseded. What it established remains in force, above all that a prefix is
one wineserver, one registry and one Windows desktop, and therefore has exactly
one screen size to give.

One unrecorded result decided this. The private diagnostic effect of
2026-09-24, built from copies of two sources under `build/` to test whether the
legacy Wine guards prevent generic presentation, ran as sx-qlj_k3z7 at 00:40 on
2026-09-25. The guards are not the obstacle. The effect selects the Wine
window, supplies 2560 × 1440 against a 3840 × 2160 destination, and the frame
settles covering the screen; the window growth of sx-d8159h3_ and sx-wrofjh_w
is gone. The run still fails, on the pointer at 3600 × 2010: the window's input
region stays 854 × 480 logical while its frame covers 1280 × 720, so a click
past roughly two thirds of the screen never reaches the game. This is the
input-shape class that L4D2 needed `updateShape()` for. No correction has been
written for it.

Wine names the program each of its processes runs, as an absolute path. In
sx-o7j42c5w the probe's connection reports
`Z:\src\build\proxy-conformance\wine-source\display-probe.exe`, while every
Wine component of the same prefix names itself below `C:\windows\system32` or
`C:\windows\syswow64`. That directory is the discriminator between a prefix's
own programs and Wine's own, so no list of helper names is needed. Wine answers
only for the process asked: the connection the proxy actually received in that
run was explorer, naming itself, and every process of the prefix is reparented
to init, so ancestry associates nothing with the game.

Decided with Jens on 2026-09-25:

- The unit of decision is the prefix, not the connection and not the program.
  Every connection of a prefix is answered the same way, from a decision the
  first connection makes and the rest reuse.
- A launcher or a second program sharing the prefix is scaled with the game.
  Two differently configured games in one prefix cannot both be served and the
  first match wins. Accepted as unlikely, and visible rather than subtle.
- A profile matches the tail of the program's path, which is the same on every
  machine the effect is installed on. A prefix path is not: library location,
  Flatpak and hand-made prefixes all differ, so it is the cache key and a
  diagnostic, never a shipped identity.
- Matching a launcher is as good as matching the game, because both reach the
  same desktop, so a profile may name either or both.
- Windowed programs are not scaled. Fullscreen and borderless present one
  window that is the desktop, which is one rectangle to scale; a windowed
  program breaks that identity and would need a moving input origin, a
  continuous resize negotiation and frame geometry KWin owns. The rule is to
  scale a window whose size matches the prefix's current screen and to present
  every other window untouched.
- The proxy resolves the identity and sends plain strings to the effect, so the
  plugin gains no platform-specific reader and stays portable.
- The resolved candidates are reported per prefix, because a profile that names
  none of them otherwise fails silently at the native size.

Checked against the code on 2026-09-28 (item 28 of the open list): the prefix is
the unit (`m_prefixPrograms` in `src/x11proxy/connection.cpp`, one answer for
every connection of a prefix, covered by `forgetsWhatAPrefixRanOnceItStops`);
a profile matches the program's path through the `wine://<prefix>/<path>`
identities the proxy builds, which a pattern anchored on the path's tail
matches from any prefix (`x11proxy_identity_test.cpp`, `matching_test.cpp`);
a launcher is matched the same way; windowed programs are left alone since the
windowed slots went on 2026-09-25; and every decision's log line names the
candidates it was matched against (`names=` in `connection.cpp`), with the
program each prefix runs logged once. Wreckfest's entry, which recognized the
game only by the window class Proton gives it, too late for the proxy, now
states the path Proton reports for the game,
`wine://<prefix>/S:/steamapps/common/Wreckfest/Wreckfest_x64.exe`, recorded
in the Wreckfest run on wzpc on 2026-10-03 (item 30). With it the game chose
2560 × 1440 in exclusive fullscreen, the effect presented it over the
3840 × 2160 output at scale 3, and Jens played it with the pointer working.

Open, in this order:

- The interval between a prefix's first connection and its first program that
  is not Wine's own. sx-o7j42c5w measured a prefix being created, where wineboot
  ran between explorer at 609507 and the program at 609612. An existing prefix
  should be far tighter, and the first connection of a Wine prefix has to be
  held until the program appears rather than answered from the ordinary 500 ms
  budget, because the program's path is the only portable identity. Unmeasured.
  Found implemented on 2026-09-28, since #21 (`src/x11proxy/connection.cpp`):
  a Wine component's connection is held only when the effect answers that the
  prefix may match (`x11PrefixMayMatch`), and then until the prefix's program
  is known, retried every 250 ms within a ten-second bound
  (`prefixDecisionMilliseconds`) rather than the 500 ms of an ordinary
  connection. `selectedWineComponentWaitsForProgram` covers it, and now waits
  for the session's own "waiting for the program of prefix" rather than for
  300 ms; it fails when the prefix is refused.
  **Measured and replaced on 2026-10-03 (item 98).** On wzpc Proton started
  `steam.exe` with the game's Unix path, the desktop 0.6 s later, and the game
  1.4 s after that; Steam first ran its install script the same way in the
  same prefix. Held for the game, the desktop held the game, which waits for
  it, and Wreckfest never connected. The launcher already names the program,
  so the proxy now takes it from there, and a component whose program a
  process names is answered at once. Only a pattern that names the prefix
  still holds it for its ten seconds; one that could match in any prefix,
  the shipped Wreckfest entry's among them, holds none, so Wine's own tools
  run on their own are never held (decided by Jens on 2026-10-03). `aLauncherNamesTheProgramBeforeItStarts`
  covers Proton's launcher in the form Wine leaves, with its loader still
  first, and with a long path.
- A prefix whose wineserver still runs from an earlier program receives no new
  connection, so its screen was decided for that program. No answer proposed.
- Acceptance across fullscreen, borderless and windowed presentation, for a
  cold prefix, a warm prefix and a launcher-first start, on real games.

### A refusal from an offscreen pass, 2026-09-25

Reading sx-qlj_k3z7 as a Wine defect cost three wrong explanations before the
measurement that settled it. The suite ran every session at output scale one,
while the acceptance machine runs its 3840 × 2160 screen at three. Closing
that gap found the defect, and it is neither Wine's nor input's.

An offscreen pass of a window - a thumbnail, a preview, anything drawn away
from the output - draws it at a scale of its own. The effect refuses to replace
such a pass, which is right, because it is not what the person is looking at.
That refusal was then kept as the reason the window was not replaced and
reported as such. On an output at scale one an offscreen pass matches the
screen and never refuses, so the effect never reported it; at another scale it
does, and the effect then says a window it is upscaling is not being upscaled.
It was this report, not the presentation, that every reading of sx-qlj_k3z7
followed. Such a pass is now refused without being remembered.

The session runner takes `--scale=`, writing the outputs and, which is what was
missing at first, a setup naming them: without one KWin generates a
configuration of its own and the recorded scale never applies. A virtual output
is matched by connector name alone, `Virtual-0` upwards, having no EDID to
hash. The new session `upscale-x11-scaled` runs the two pointer-coverage tests
at scale three; the sessions confirm 1280 × 720 logical on a 3840 × 2160
output.

Both of those tests fail at scale three before this correction and pass after
it, and the same mistake the Wine harness made was in them: they placed the
pointer in device pixels. They now convert, and a new `coversPointerWithoutEmulatedMode`
covers a program that never asks for a mode and is therefore presented by the
effect rather than by Xwayland's emulation, which is what a Wine game is. The
assertion that no offscreen pass is reported was checked against the unfixed
effect and fails there.

All 27 suites pass in Trixie with GCC.

### A borderless window covers the screen it was given, 2026-09-25

Jens asked on 2026-09-25 whether a borderless window at the size of the
display was still supported, after the windowed method slots were removed.
It was, and it would also have failed for exactly the games this route exists
for.

Fullscreen survives because it is a state: KWin gives such a window the
output's geometry whatever size its client is, so the effect only has to hold
the client smaller underneath. Borderless has no state to read and is judged on
geometry alone - `upscaleCoversOutput()` required the window's frame edges to
match the output's. A game whose connection has been answered with a smaller
screen makes a borderless window that size, because the proxy rewrites what the
client is told and not Xwayland's root, which stays at the output's size. Such
a window covers part of the output, is classified windowed, and is refused.

The rule this slice already records answers it: a window covers *its* screen,
and for a program whose connection was answered that is the size advertised
rather than the output. The effect now keeps that size beside the process it
answered for, and measures coverage against it. Nothing about the test is Wine
specific, and no size is written into the code: the case asks the effect's own
connection policy what screen it would advertise and makes a window of exactly
that.

The new case fails before the correction with the refusal it predicts, "not
fullscreen or a selected borderless window", and passes after it. It runs in a
session with one 4K screen at scale three, which is the acceptance machine's
own arrangement; a connection is answered before any window exists, so what an
answer names is a screen and not an arrangement of them, and the session that
runs two outputs skips the case for that stated reason alone.

All 27 suites pass in Trixie with GCC.

### Steam as Flatpak and Snap, 2026-09-27

Proposed by Jens, 2026-09-27, alongside SuperTuxKart and Extreme Tux Racer in
their two repackagings, which belong to
[the applications we know about](slice-application-profiles.md#the-same-games-repackaged-flatpak-and-snap-2026-09-27).
This slice owns Steam's two forms, because what they change is the prefix and
the process the prefix is found from.

Both exist, read from the Flathub and Snap Store APIs on 2026-09-27; nothing
was installed and nothing was run. `com.valvesoftware.Steam` 1.0.0.87, x86_64
only, with `--socket=x11`, `--socket=wayland` and `--device=all`. The `steam`
Snap 1.0.0.87 on amd64 and 1.0.0.85 on arm64, strict confinement on `core24`,
published by Canonical as a verified publisher and released this month, so it
is maintained rather than abandoned. On this host `flatpak` 1.16.6 has no
remote configured and no application installed, and `snapd` is absent.

What this costs the design, in the order [locating the prefix](#locating-the-prefix)
asks its questions:

- **A moved Steam root costs nothing, by construction.** Under the Flatpak,
  Steam's root is below `~/.var/app/com.valvesoftware.Steam/`, so
  `STEAM_COMPAT_DATA_PATH` and `WINEPREFIX` move with it. The chain never
  derives that path: it reads `WINEPREFIX` from the process's own environment
  and resolves every path through `/proc/<pid>/root`. The cross-check that
  `WINEPREFIX` equals `$STEAM_COMPAT_DATA_PATH/pfx`, and that the directory
  name equals `SteamAppId`, is unaffected by where the root sits. This is the
  part expected to work, and it is expected to work without a change.
- **The PID is the hazard, and the two containers do not share it.** Flatpak
  runs bubblewrap with `--unshare-pid`, verified in the 1.16.6 binary on this
  host; `flatpak run` can share a PID namespace only with a parent instance.
  Every step here starts from the PID KWin reports, and on KWin 6.3.6 an X11
  window's PID is the client's `_NET_WM_PID`, which a Proton game inside the
  Flatpak sets from inside that namespace. It therefore names another process
  on the host, and the companion must refuse and say so rather than write.
  **That refusal is the correct result, not a failure**: the acceptance criteria
  already require a refusal for every mismatch, and this is one. What has to be
  shown is that it refuses and names the reason - never that it succeeds. On
  KWin 6.6.6 and master, where the PID comes from the X-Resource extension and
  so from the connection, it is expected to resolve; that is to be confirmed.
- **The Snap is expected to keep a valid PID**, since strict confinement is not
  known to unshare the PID namespace, which would make it the one container
  where the whole chain can run through. Expected, and to be observed.
- **pressure-vessel adds no further namespace to reason about.** This document
  already records that it shares the PID namespace, so the container's
  namespace is the one that decides, not a second one inside it.
- **The server lock is already resolved the right way.** The lock at
  `<tmp>/.wine-<uid>/server-<st_dev>-<st_ino>/lock` is looked up through
  `/proc/<pid>/root`, so the container's own `/tmp` is seen as the game sees
  it. Whether the same-user read of `/proc/<pid>/environ` and `/proc/<pid>/root`
  is permitted at all under each container's Yama and user-namespace setup
  stays the open question the existing text raises, and these two packagings
  are how it gets answered.

This is [full acceptance](#gates) work, which already names Flatpak and Snap
installs, and it closes nothing in the supported scope. It does not join the
current candidate's baseline: Jens's hardware acceptance sequence stays
Wreckfest, Extreme Tux Racer, SuperTuxKart and Left 4 Dead 2 natively, and
these experiments follow it.

### A confined pointer cannot reach the whole game, 2026-09-29

Found while checking real Wine input at scale 3 (item 29 of the open list), in
the conformance machine: a Windows OpenGL program under Wine 10.0, asking for
2560 × 1440 in exclusive fullscreen, through the session proxy, at 3840 × 2160
and scale 3, presented by the effect over the whole output. A pointer device
added by a small test plugin (`upscale_test_pointer`, kept with the probe under
`build/wine-probe/` until a regression test uses it) moved to logical positions
while the program logged every `WM_MOUSEMOVE`. (640, 360) arrived as
(1280, 720) and (100, 50) as (200, 100), as the effect's mapping intends. Then
Wine confined the pointer to its window, and Xwayland passed that on as a
pointer constraint over the window's surface: 854 × 480 logical, the 2560 ×
1440 pixels at scale 3. KWin 6.3.6 checks a confinement in the surface's own
coordinates, `Window::mapToLocal()`, which subtracts the buffer's position and
knows nothing of the effect's presentation, and it applies the check before any
input filter. So the pointer stays in the top left 854 × 480 of the 1280 × 720
screen: (1200, 700) did not move it, and (320, 540) moved it in x alone
(arriving as 640, 100). Through the effect's mapping the program then receives
at most two thirds of its range in each direction, and the right and bottom
third of the game cannot be reached while it confines the pointer, which many
fullscreen games do. A pointer lock, as mouse look uses, is handled already.

The lasting fix is the one this slice already names for KWin: a per-window
presentation transform that the input path honours, `mapToLocal()` and the
constraint checks among it. Until then the effect has only workarounds, each
with a cost: mapping one to one while the pointer is confined (the program
gets its whole range, but the system cursor is drawn where KWin keeps it), or
undoing KWin's confinement and clamping in the effect's own filter (KWin
engages it again whenever the pointer is inside the small region). Which to
take is Jens's decision (item 29a of the open list).

Decided by Jens on 2026-09-29: propose the KWin change upstream (item 29b), and
map one to one while confined on KWin versions without it. The second half is
implemented: while a presented surface's confinement is engaged,
`UpscaleX11Input` passes positions and relative motion with scale one, so the
program reaches its whole window and a system cursor, where one is shown, is
drawn where KWin keeps it. `aConfinedPointerReachesTheWholeWindow`, in the
scale-3 session, confines the pointer with the cursor shown as Wine does and
receives 1800 × 1050 at that point of the output, where the old mapping gave
900 × 525. With real Wine 10.0 in the machine, the same OpenGL probe at scale 3
through the proxy, confined by Wine: the test pointer at logical (640, 360),
(100, 50) and (850, 475) arrived as (1920, 1080), (300, 150) and
(2550, 1425), the last its window's lower right, which before the change
could not be reached beyond about (1706, 960). A position outside the
confinement is dropped by KWin for this absolute test device, where a real
mouse's relative motion stops at the edge; input on a real display is checked
with Jens (supported scope, item 94a).

Under KWin 6.6.6 and Xwayland 24.1.10 of Kubuntu 26.04 the case first waited in
vain: Xwayland asked for a lock, not a confinement. A window that sets no cursor
of its own, as the test client's did, counts there as one that hides the
cursor, and a grab with the cursor hidden is mouse look to Xwayland
(`xwl_seat_maybe_lock_on_hidden_cursor`); Trixie's 24.1.6 under KWin 6.3.6
confined the same window. Wine always sets a game's cursor on its window, so the
test client now does too, with the arrow of the server's cursor font, and
Xwayland confines under both: the case passes on 6.3.6 and 6.6.6. The case also
showed a race in the X11 session's `movePointer`, which returned before the
driver in KWin had read the request: a window that mapped under the pointer
got its crossing from the X server alone, the next case's wait for motion was
already met, and its next move replaced the unread one. `movePointer` now
waits until the driver has taken the request, as the prepared session's
`request()` did already.

### Wine's Wayland driver, reproduced, 2026-09-29

Item 35 of the open list, first by observation rather than from Wine's source.
The same Windows OpenGL probe under Debian's Wine 10.0 with the prefix's
graphics driver set to `wayland`, in the conformance machine at 3840 × 2160 and
scale 1, with an entry naming Wine's loader and AdvertisedMode on the Wayland
fullscreen slot. Wine heard the 2560 × 1440 mode at bind, its screen was 2560 ×
1440 and so was its window, which stayed fullscreen. The effect refused it:
"the window does not exactly cover its output", with a supplied buffer of
2560 × 1536, taller than the surface Wine reports, where the X11 path presents
the same program over the whole output. Two findings on the way: Wine 10.0's
Wayland driver locks the pointer for a fullscreen window without checking that
the seat has one, and dies on a seat without a pointer (`lock_pointer` with a
null `wl_pointer`), which KWin's virtual backend is until a pointer device is
added; and the probe and its runner stay scratch under `build/wine-probe/`.

Presenting it needs what the X11 path has and the Wayland path lacks: drawing a
fullscreen window whose surface is smaller than its output over the whole
output, with the viewport's source rather than the buffer's size, and mapping
the pointer onto the smaller surface, which on Wayland meets the same unscaled
constraint check as the confined pointer above. Not implemented yet.

The Auto bench of 2026-09-29 (item 19, in the
[resolution-control slice](slice-resolution-control.md#the-bench-run-2026-09-29))
ran a Direct3D 11 sample the same way, borderless and in exclusive fullscreen,
on Wine's own Direct3D, and found one more obstacle: with nothing told, at the
full 3840 × 2160, the effect refused the window because Wine draws into a
subsurface ("the window's surface has child surfaces"). The buffer heights are
rounded up to a multiple of 128 there too, 2176 for 2160. A presentation for
Wine's Wayland driver therefore has to take the subsurface's image and its
viewport's source, not the main surface's buffer.

The same bench's Wine runs through the proxy (item 20) found that a program Wine
is started with by its Unix path, `wine /path/game.exe`, keeps that path in its
command line rather than a Windows one, and that the proxy never identified it:
its prefix's connections waited their ten seconds for a program that never
showed, and the program then saw a 1024 × 768 screen. The proxy now names it on
the drive whose directory holds it most closely, as Wine does, from the
prefix's `dosdevices`: `wine:///<prefix>/Z:/path/game.exe` for a program
anywhere, `C:` for one inside the prefix's own drive. How Proton names its
games was not observed here; that stays with item 30.

### A warm prefix, measured and fixed, 2026-09-29

Item 27 of the open list. The Windows OpenGL probe under Debian's Wine 10.0,
through the proxy in the conformance machine at 3840 × 2160, with an entry
naming only the probe. The probe logs the screen Wine reports at its start.

| Start | Before | After |
| --- | --- | --- |
| The probe alone in a new prefix | 2560 × 1440 | 2560 × 1440, nothing switched |
| An unlisted Windows program first, the probe 20 s later | 3840 × 2160 | 2560 × 1440 |
| Notepad first, the probe 20 s later | 3840 × 2160 | 2560 × 1440 |

Before, the earlier connections were answered at full size - the unlisted
program's as unlisted, Wine's components after their ten-second wait - and
Wine's desktop process had read the screen through them, so the probe's own
smaller answer came too late. Now the session keeps each prefix's connections,
and when a program the effect selects is answered with a smaller screen, every
earlier connection is shown that screen and sent the RandR events it selected
on the root. Wine's desktop process selects CrtcChange, OutputChange and
ProviderChange on the root, received one RRNotify, and re-read its displays
at that moment (its `xrandr14_get_gpus` traces). The first measurement of the
change still gave 3840 × 2160: the connection explorer selected on was opened
by another of its threads, and a process's later connections were relayed
without their prefix, so they were never switched. The session test now opens
two connections per stand-in process.

Found on the way and kept apart as 14a: the "launcher" was a copy of the probe,
and both copies set 2560 × 1440 with ChangeDisplaySettings (the runner's
arguments reached neither, so both ran with their defaults); the first one's
window then grew by 1280 × 720 on each size notification, up to X's limit,
and a Wine process aborted in libxcb. This happened before the change as well,
with the proxy and not without it.

Traced later the same day with `WINEDEBUG=+x11drv,+win,+system` on the
launcher. When the prefix is shown 2560 × 1440, Wine re-reads its displays, and
win32u's `map_window_rects_virt_to_raw()` gives the launcher's window, whose
visible part covers the old monitor, the whole new raw monitor as its visible
part: window 3840 × 2160, visible 2560 × 1440. Wine asks X for the visible
size; KWin keeps the fullscreen window at the output's 3840 × 2160, as it keeps
every fullscreen window it was not asked to size otherwise; and
`window_rect_from_visible()` takes the window as that X size plus the
difference between window and visible part, 5120 × 2880, which is cropped
again and grows by 1280 × 720 at each answer. The cause is the disagreement
between the screen the proxy shows the prefix and the size KWin gives a
fullscreen window of it that the effect does not present; only the selected
game's window is sized to the smaller screen. The two ways out, presenting the
prefix's other fullscreen windows at the game's screen too or not showing the
smaller screen to a connection that has a fullscreen window, change what
happens to a program no entry lists, and are Jens's to choose.

Jens chose the first the same day: one prefix is one screen. Built: the proxy
keeps the screen a prefix shows since its game was answered, with the game's
process; a later connection of the prefix, listed or not, is shown it too;
and every process shown it is reported to the effect (`x11ProcessShown`),
which claims that process's windows with the entry that answered the game and
looks at them at once. A process that already runs is switched only after the
effect has answered its report, at most half a second later, so that its
fullscreen window is 2560 × 1440 before Wine asks for that size. Run again in
the conformance machine: the effect resized the launcher's window, the prefix
switched five connections, the launcher reported 2560 × 1440 and was
presented, first by the effect and then by Xwayland's emulation once Wine set
the mode for it, and the probe saw 2560 × 1440 as before.

### Proposal prepared for KDE: a presentation transform KWin's input honours (item 29b), 2026-09-29

Decided by Jens on 2026-09-29 to propose; filing is his, with 2e's report. The
text, for KWin's issue tracker or as a merge request description:

> **An effect that presents a window at another size than its surface cannot
> tell KWin's input about it**
>
> An effect can draw a window's surface larger than the surface is, across its
> output, the way an upscaler presents a game that renders at 2560 x 1440 on a
> 3840 x 2160 output. Pointer input can follow through an input event filter,
> which moves focus and sets the seat's surface transformation. But KWin's own
> checks work in the surface's coordinates without it:
> `PointerInputRedirection::applyPointerConfinement()` and
> `updatePointerConstraints()` test the confinement and lock regions with
> `Window::mapToLocal()`, which is `point - bufferGeometry().topLeft()`, and
> `Window::hitTest()` does the same. So a confinement the client asks for,
> which Wine does for every fullscreen game, keeps the pointer inside the
> unscaled part of the picture: at 2560 x 1440 on 3840 x 2160 the pointer
> stops at two thirds of each side (measured with Wine 10.0 on KWin 6.3.6: the
> game's lower right beyond about 1706 x 960 was out of reach).
>
> Proposal: a per-window presentation transform, set by an effect through
> `EffectWindow` and cleared when it stops presenting, which
> `Window::mapToLocal()`, `mapFromLocal()`, `inputTransformation()` and the
> pointer constraint checks apply after the buffer geometry. Without one set,
> nothing changes. With it, focus, hit testing, confinement and locks all agree
> with what the user sees, and an effect no longer has to replace KWin's
> pointer handling in a filter to get there.
>
> Until then the upscaler maps the pointer one to one while a confinement is
> engaged, so the game reaches all of its window, and a system cursor, where
> one is shown, is drawn where KWin keeps it rather than where the game draws
> its own.

The measurements behind it are the section on the confined pointer above.
