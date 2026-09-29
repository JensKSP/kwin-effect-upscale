<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: known limitations, one by one

## Current status

Created 2026-09-29 on Jens's request, to take the cases where the effect does
not do what it should one at a time: where a game's buffer is not made
smaller, where the picture is not right, and where the pointer is not where the
player sees it. Collected from the handbook's
[known compatibility limits](../upscaling.md#known-compatibility-limits), its
eligibility and presentation sections, the open list and the other slices, and
from the work of 2026-09-29. Nothing below has been started in this slice yet;
the entries record what was known when it was made.

Each limitation has an identifier here, **K1** onwards, which this document
alone uses. An entry names the evidence, what the player sees, what the status
says, the gate it belongs to, and the next step. Where another slice or an
item of the open list already holds the work, the entry links there, and the
link is the only place its status is kept.

## Start state and evidence

The effect enlarges a game's buffer where it can get a smaller one and
present it whole, and otherwise falls back to KWin's own rendering, naming the
reason in its status. The limits below are what is known not to work, or not
yet to have been seen to work. Each rests on a measurement, a test or a reading
of the source named in its entry; none is a guess about programs nobody ran.

## End state

Every limitation below is in one of two states:

- **Removed:** fixed in the effect or the proxy, with a test that failed before
  the fix and passes after it, and the handbook updated.
- **Stated:** it cannot be removed from the effect, or Jens decided not to, and
  the handbook's known limits name it with what the player sees and what the
  status says, and an upstream report is filed where the fix belongs upstream.

This document is deleted once no entry is left open; what lasts goes to the
handbook.

## Scope and exclusions

In scope: a game's buffer not made smaller, a picture that is refused or
wrong, and pointer input that does not land where the picture is, for the
programs and presentations the handbook requires.

Excluded, each with its owner:

- HDR and VRR, postponed to a later version by Jens (item 75), with
  [FSR rendering](slice-fsr1-hdr-vrr.md).
- Windowed games, which the effect leaves to KWin by design.
- Acceptance on real hardware as such, which stays with the slice that owns
  the requirement ([resolution control](slice-resolution-control.md) for input
  on a real display, item 94a); an entry here links to it where a limitation
  waits on it.
- Features Jens deferred: the display shortcut (61) and interactive controls
  (69).

## Dependencies

- [Resolution control](slice-resolution-control.md) and
  [Proton smaller screen](slice-proton-smaller-screen.md) for the methods and
  the X11 presentation these limits are about.
- KWin upstream for K20: the proposal text for a per-window presentation
  transform is prepared in the Proton slice (item 29b); filing is Jens's.
- [Package machines](slice-package-machines.md) for the systems and container
  formats of K11 and K13.

## Approach

One entry at a time, in the order Jens chooses; the order proposed at the end
is only a proposal. For each: reproduce it with a test that fails, or with a
recorded run where no test can, then either fix it and show the test passing,
or bring the handbook's known limits and the status in line and, where the fix
is upstream's, prepare the report. The entry records what ran and what it
showed.

## Acceptance criteria

- Each entry is removed or stated, as defined above.
- A removed entry has a test that failed before its fix, run in the
  containers, the conformance machine or, where only hardware can show it,
  Jens's session.
- The handbook's known limits list every stated entry, and no entry that was
  removed.

## Supported scope and full acceptance

The [handbook](../upscaling.md#supported-scope-and-full-acceptance) defines both
gates. For this package:

**Supported scope, releasable.** Every limitation that touches a case inside
the release's supported scope is removed, and every other one is stated: the
handbook names it and the status reports it, as the supported scope requires of
every excluded case. Entries marked **S** below touch the supported scope.

**Full acceptance.** Every entry removed where the effect or the proxy can
remove it, and the rest stated with their upstream reports filed. Entries
marked **F** belong here.

## The buffer is not made smaller

| | Limitation | Evidence | What the player sees, what the status says | Gate | Next |
| --- | --- | --- | --- | --- | --- |
| K1 | Wayland programs that follow neither lever: Qt, and SDL 3 without high pixel density | The bench of 2026-09-29 in the resolution-control slice: Qt Quick 6.8 clamps the scale, SDL 3.2.10 desktop fullscreen keeps 3840 × 2160 | The game at full size, as without the effect. The status shows what was asked beside the full-size buffer, and "Inactive: the supplied buffer is not smaller than the destination." | F, outside the scope of Wayland Auto (19a) | Read Qt's and SDL 3's Wayland backends for any lever they do follow; otherwise stated |
| K2 | A mode and an integer scale cannot say two thirds at scale 1 | vkmark 2025.01 binds neither `wp_fractional_scale_v1` nor `wp_viewporter`, only an integer buffer scale (bench); the handbook's row names glmark2 2023.01 on Wayland beside it | Full size at Quality on a scale-1 screen; only Performance's scale 2 reaches it. The status names the reachable request apart from the wish | F | Stated already in the handbook; check whether a mode alone can reach vkmark's swapchain |
| K3 | Wine's Wayland driver | Item 35: the window stays at the told 2560 × 1440; it draws into a subsurface, which the effect refuses even at full size, and rounds buffer heights up to a multiple of 128 | A smaller window, not enlarged. The status names child surfaces or the coverage | F | Present a window whose picture is in one subsurface, with the pointer mapped; shares K20's confinement gap |
| K4 | A program that connected before it could be told | The Wayland mode is told when a client binds the output, the X11 screen when a connection goes through the proxy; a program already running, or an X11 program that does not connect through the proxy, hears neither | Full size, or an X11 program resized after it started showing part of its picture enlarged (glmark2 2023.01, SuperTux 0.6.3 keep their first viewport). For a Wayland program the status names the size "from the next start"; not checked for every route | S, stated | A test per route that the status says so |
| K5 | A toolkit that picks another output | Extreme Tux Racer 0.8.4 with SFML 2.6.2 moves to the primary output when it recreates its fullscreen window | The shipped entry refuses control on a secondary output; the status says so | S, stated | Stays stated unless SFML changes |
| K6 | A requested X11 mode the program does not list | SFML validates fullscreen modes against its list; the fixture refuses 2259 × 1271 on the 4K output | The request is refused and named; the game stays at full size | S, stated | Offer the nearest listed mode instead, as the Wayland scale does with reachable steps |
| K7 | Internal render targets that keep their size | SuperTux 0.6.3: the outer buffer and viewport went from 4K to 1080p, an intermediate framebuffer stayed 1368 × 769 | Nothing visible; the saving in GPU time is smaller than the buffer suggests | F | A measurement question for the cost matrix (item 74), not a defect; stated |
| K8 | A game that keeps the smaller size in its own settings | SuperTuxKart 1.4 writes the told mode into its configuration (item 17) | Under Native the effect enlarges the kept smaller buffer; under other presets the display marks the kept size in the warning colour | S, stated | Say it in the handbook's known limits; nothing to change in the effect |
| K9 | Programs in Flatpak and Snap | Item 51: Flatpak's SuperTuxKart 1.5 on Wayland was claimed and asked in the Fedora 43 machine; the X11 route through the proxy and Snap are not observed; item 47's naming is not yet seen in a session; Steam as Flatpak or Snap is item 34 | Unknown where not observed | F | 51's remainder and 34, in the package machines |
| K10 | Wine and Proton across the graphics paths | Only Direct3D 11 on Wine's own renderer and OpenGL were run; official Proton and Direct3D 9, 12 and Vulkan, exclusive and borderless, are item 36 | Unknown where not run | F | Item 36 |
| K11 | FreeBSD and the BSDs | FreeBSD's nightly installs, loads and removes the effect in an emptied machine; no session check, and the proxy is not run there (items 2f, 23) | Unknown | F | The session check once the virtio-gpu driver allows one |

## The picture is refused or not right

| | Limitation | Evidence | What the player sees, what the status says | Gate | Next |
| --- | --- | --- | --- | --- | --- |
| K12 | Presentations the effect refuses | The eligibility section of the handbook: rotated or transformed outputs and windows, child surfaces, cropped or transformed buffers, a buffer below half the destination, beyond FSR's twofold range, no whole factor that fits, colour descriptions the shaders cannot decode, unknown buffer formats | KWin's own scaling, as without the effect. The status names each refusal | S, stated | Decide with Jens which to lift; child surfaces go with K3, beyond twofold would need a second pass |
| K13 | Mixed output scales, hotplug and output movement in a real session | Tested in the virtual backend only; physical acceptance is item 22 | Unknown on hardware | F | Item 22 |
| K14 | What a window drawn over its output covers | Item 18: while it is active, panels, windows kept above and ordinary notifications on its output are left out of the picture, as KWin does for an active fullscreen window | Those windows are hidden while the game is active | S, by design | Stated in the handbook already; nothing to change unless Jens decides otherwise |

## The pointer is not where the picture is

| | Limitation | Evidence | What the player sees, what the status says | Gate | Next |
| --- | --- | --- | --- | --- | --- |
| K15 | A confined pointer passes one to one | Item 29: KWin checks a confinement in the surface's own coordinates, before any filter; the interim mapping of 29a lets Wine 10.0 reach all of its window at scale 3 | The game reaches all of its window, but a cursor the system draws is drawn where KWin keeps it, not over the picture | S, interim | The KWin proposal of 29b, filed by Jens; then map through KWin's transform |
| K16 | Touch and tablet input | The presentation section of the handbook: not mapped | Touch and pen positions are not mapped to the picture | F | Map them as the pointer is, with tests through KWin's input |
| K17 | Confinement regions and the locked pointer's position hint | The presentation section: not mapped | A confinement to part of a window, and a position hint on unlock, apply in the window's unscaled coordinates | F | Map both through the presentation, with K15 |
| K18 | The cursor over a hidden decoration | Item 18: the pointer's motion is the game's there, but which cursor KWin shows was not looked at | Possibly a resize cursor at a hidden window border | S | Look in the conformance machine with a decorated window beneath a presented game |
| K19 | A visible dialog's title bar above a presented X11 game | Item 18b: the filter now asks KWin's hover window; no test, because no test session can present a game with a decorated window above it | The dialog should keep its title bar | S | Find a way to stack a decorated window above a presented one in a test session |
| K20 | Wine's Wayland driver's pointer | With K3: a smaller surface needs the same mapping, and meets K15's confinement | – | F | With K3 |

## Proposed order

Jens decides the order. Proposed, cheapest and nearest the supported scope
first:

1. Bring the handbook's known limits up to date with the entries marked
   stated, and correct its row on smaller windows, which item 18 changed.
2. K18 and K19, both small and inside the supported scope.
3. K4, a test per route that the status says "from the next start".
4. K6, the nearest listed X11 mode.
5. K15 once KWin answers the proposal, K16 and K17.
6. K3 with K20, then K1 and K2.
7. K9, K10, K11 and K13 with the machines and hardware they need.

## Progress

Nothing yet.
