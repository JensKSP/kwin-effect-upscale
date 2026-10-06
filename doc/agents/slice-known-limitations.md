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
from the work of 2026-09-29. The entries record what was known when it was
made; work started on 2026-10-04, in the order under Progress.

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
| K1 | Wayland clients with different buffer-density policies | The original bench found Qt and SDL 3 without high pixel density ignoring the scale hint. The [scale-one Auto work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29) owns the configure fallback, real-display results and comparison across desktop scales. | A smaller configure reaches these clients at scale one; low-density SDL at higher desktop scales can still supply a buffer below the supported range. Status reports the supplied size and refusal. | F; scale-one coverage extended by the linked work | Follow the linked density comparison and remaining acceptance; not a universal geometry-only solution |
| K2 | Integer scale and startup-only sizing | vkmark ignores configure sizes; glmark2 keeps the viewport initialized from its first fullscreen configure. The [scale-one Auto work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29) owns their separate fixes and the native comparison. | vkmark can use the advertised mode; fresh glmark2 can use a smaller first configure. A late glmark2 resize crops the scene despite a smaller buffer. Integer-only density still differs on fractional desktops. | F | Preserve startup timing and actual-picture checks; live changes to fixed viewports remain open |
| K3 | Wine's Wayland driver | Item 35: the window stays at the told 2560 × 1440; it draws into a subsurface, which the effect refuses even at full size, and rounds buffer heights up to a multiple of 128 | A smaller window, not enlarged. The status names child surfaces or the coverage | F | Present a window whose picture is in one subsurface, with the pointer mapped; shares K20's confinement gap. In the [Proton slice](slice-proton-smaller-screen.md#remaining-work) |
| K4 | A program that connected before it could be told | The Wayland mode is told when a client binds the output, the X11 screen when a connection goes through the proxy; a program already running, or an X11 program that does not connect through the proxy, hears neither | Full size, or an X11 program resized after it started showing part of its picture enlarged (glmark2 2023.01, SuperTux 0.6.3 keep their first viewport). For a Wayland program the status names the size "from the next start"; not checked for every route | S, stated | Tested per route 2026-10-06, see Progress; stays stated |
| K5 | A toolkit that picks another output | Extreme Tux Racer 0.8.4 with SFML 2.6.2 moves to the primary output when it recreates its fullscreen window | The shipped entry refuses control on a secondary output; the status says so | S, stated | Stays stated unless SFML changes |
| K6 | A requested X11 mode the program does not list | Removed 2026-10-06: the nearest listed mode is asked instead; see Progress | The game gets the listed mode nearest to the wish, enlarged | S | Done |
| K7 | Internal render targets that keep their size | SuperTux 0.6.3: the outer buffer and viewport went from 4K to 1080p, an intermediate framebuffer stayed 1368 × 769 | Nothing visible; the saving in GPU time is smaller than the buffer suggests | F | A measurement question for the cost matrix (item 74), not a defect; stated |
| K8 | A game that keeps the smaller size in its own settings | SuperTuxKart 1.4 writes the told mode into its configuration (item 17) | Under Native the effect enlarges the kept smaller buffer; under other presets the display marks the kept size in the warning colour | S, stated | Stated in the handbook's known limits since 2026-10-04; nothing to change in the effect |
| K9 | Programs in Flatpak and Snap | Item 51: Flatpak's SuperTuxKart 1.5 on Wayland was claimed and asked in the Fedora 43 machine; the X11 route through the proxy and Snap are not observed; item 47's naming is not yet seen in a session; Steam as Flatpak or Snap is item 34 | Unknown where not observed | F | 51's and 47's remainder in [application profiles](slice-application-profiles.md#progress-and-remaining-work), 34 in the [Proton slice](slice-proton-smaller-screen.md#remaining-work), run in the package machines |
| K10 | Wine and Proton across the graphics paths | Only Direct3D 11 on Wine's own renderer and OpenGL were run; official Proton and Direct3D 9, 12 and Vulkan, exclusive and borderless, are item 36 | Unknown where not run | F | Item 36, in the [Proton slice](slice-proton-smaller-screen.md#remaining-work) |
| K11 | FreeBSD and the BSDs | FreeBSD's nightly installs, loads and removes the effect in an emptied machine; no session check, and the proxy is not run there (items 2f, 23) | Unknown | F | The session check once the virtio-gpu driver allows one |

## The picture is refused or not right

| | Limitation | Evidence | What the player sees, what the status says | Gate | Next |
| --- | --- | --- | --- | --- | --- |
| K12 | Presentations the effect refuses | The eligibility section of the handbook: rotated or transformed outputs and windows, child surfaces, cropped or transformed buffers, a buffer below half the destination, beyond FSR's twofold range, no whole factor that fits, colour descriptions the shaders cannot decode, unknown buffer formats | KWin's own scaling, as without the effect. The status names each refusal | S, stated | Decide with Jens which to lift; child surfaces go with K3, beyond twofold would need a second pass |
| K13 | Mixed output scales, hotplug and output movement in a real session | Tested in the virtual backend only; physical acceptance is item 22 | Unknown on hardware | F | Item 22, in [resolution control](slice-resolution-control.md#independent-output-policy-and-pixel-threshold) |
| K14 | What a window drawn over its output covers | Item 18: while it is active, panels, windows kept above and ordinary notifications on its output are left out of the picture, as KWin does for an active fullscreen window | Those windows are hidden while the game is active | S, by design | Stated in the handbook already; nothing to change unless Jens decides otherwise |

## The pointer is not where the picture is

| | Limitation | Evidence | What the player sees, what the status says | Gate | Next |
| --- | --- | --- | --- | --- | --- |
| K15 | A confined pointer passes one to one | Item 29: KWin checks a confinement in the surface's own coordinates, before any filter; the interim mapping of 29a lets Wine 10.0 reach all of its window at scale 3 | The game reaches all of its window, but a cursor the system draws is drawn where KWin keeps it, not over the picture | S, interim | The KWin proposal of 29b, filed by Jens; then map through KWin's transform |
| K16 | Touch and tablet input | The presentation section of the handbook: not mapped | Touch and pen positions are not mapped to the picture | F | Map them as the pointer is, with tests through KWin's input |
| K17 | Confinement regions and the locked pointer's position hint | The presentation section: not mapped | A confinement to part of a window, and a position hint on unlock, apply in the window's unscaled coordinates | F | Map both through the presentation, with K15 |
| K18 | The cursor over a hidden decoration | Removed 2026-10-06: KWin gave the hidden decoration the pointer and showed its cursor; both filters now take it away where they claim the pointer, see Progress | The game's cursor | S | Done |
| K19 | A visible dialog's title bar above a presented X11 game | Removed 2026-10-06: tested, the title bar keeps the pointer; see Progress | The dialog keeps its title bar | S | Done |
| K20 | Wine's Wayland driver's pointer | With K3: a smaller surface needs the same mapping, and meets K15's confinement | – | F | With K3 |
| K21 | Input to a picture with bars beyond the absolute pointer | Tested 2026-10-06, see Progress: relative motion and a locked pointer are mapped under Fit and Integer, a popup over a bar takes its own pointer; subsurfaces are refused (K12), overlays drawn into the game's buffer are its picture | Relative motion and a lock as the game expects; a popup at its unscaled place | F | Stays stated for popups' placement |

## Proposed order

Jens decides the order. Proposed, cheapest and nearest the supported scope
first:

1. Bring the handbook's known limits up to date with the entries marked
   stated, and correct its row on smaller windows, which item 18 changed.
2. K18 and K19, both small and inside the supported scope.
3. K4, a test per route that the status says "from the next start".
4. K6, the nearest listed X11 mode.
5. K15 once KWin answers the proposal, K16, K17 and K21.
6. K3 with K20, then K1 and K2.
7. K9, K10, K11 and K13 with the machines and hardware they need.

## Progress

Jens confirmed the proposed order on 2026-10-04. Step 1 is done the same day:
the handbook's [known compatibility limits](../upscaling.md#known-compatibility-limits)
now state K4, K5, K6, K8 and K12, and the input K15 to K17 and K21 leave
unmapped; the row on smaller windows says what item 18 and its windowed
follow-up made of it, presentation over the whole output with what it covers
(K14) and the confined pointer's interim mapping (K15); and the coverage row
names what is still unverified. Next is step 2, K18 and K19.

K18, removed 2026-10-06. Looked at in the conformance machine with
`aHiddenDecorationKeepsTheGamesCursor`, a production case in KWin 6.3.6's own
test framework (`autotests/kwin_presentation_test.cpp`, the production test's
class now in `kwin_scaling_test.h`): a window with Breeze's server-side
decoration under a game of the told size drawn over the 384 × 216 output, the
pointer over the hidden title bar and over the hidden top-left corner. KWin's
hover was the window beneath and the seat's pointer was on the game, as the
filter sets it, but KWin had given the hidden decoration the pointer in its
`update()`, which runs before any filter, and `CursorImage` shows a
decoration's cursor whenever one has the pointer: there KWin showed Breeze's
shape cursor where the game showed its own. `UpscalePictureInput` and
`UpscaleX11Input` now clear KWin's decoration focus where they claim the
pointer, through the public `InputDeviceHandler::setDecoration()`, present in
6.3.6 and master; KWin then shows the cursor of the surface the seat is on.
The case failed on the old plugin and passes; all 18 production cases pass, and
the Wayland and X11 integration suites in the container. Reading the client's
own cursor back was tried first and given up: in this harness a test client's
`wl_pointer` heard no enter, so the case asserts the condition `CursorImage`
decides by.

K19, removed 2026-10-06. A decorated window above a presented X11 game was
never impossible in a test session, only not tried: in the X11 integration
suite, `leavesATitleBarAboveAPresentedGame` opens a window with Aurorae's
Plastik decoration over a fullscreen game the effect presents, checks that the
game is still presented beside it (a motion there reaches the game halved), and
moves the pointer along the window's title bar. The test driver now reports
whose decoration KWin gave the pointer (`pointerDecoration`): the window's
own, and the game hears neither motion. No change to the plugin was needed;
item 18b's question of KWin's hover window holds. The same driver property
gives K18 its X11 half: `movesThePointerOverAHiddenTitleBar` now also asks that
no decoration keeps the pointer over a title bar the picture hides. Both cases
pass, and both fail with the X11 filter's two checks taken out. Step 2 is done;
next is step 3, K4.

K4, tested per route 2026-10-06; it stays a stated limit, since a program that
has bound the output or opened its connection is not told again. A Wayland
program hears its mode when it binds the output, and the report says what its
next start brings on each route: told nothing because the wish was Native at
its start, told a mode before the wish moved (already covered by
`asksApplicationsForASmallerImage`), and told a mode before the wish became
Native. `aWishAfterTheStartWaitsForTheNext` covers the two that had only unit
tests, with a client that takes no surface scale; both passed as the code
stood. Such a window is still asked for a surface scale while it runs, and the
report names that request until the window has drawn past the effect's
patience, 30 frames, unanswered; the case draws 35 before it reads the report,
which a full run under load showed it has to. An X11 program is
told its screen by the proxy at connection, and the X11 status promises no
next start: it reports the resize asked of the window and what the window
did, while the handbook states that a program resized after it started can
keep the viewport it began with, which the effect cannot see. Step 3 is done;
next is step 4, K6.

K6, removed 2026-10-06. The X11 resize asked for the wish's exact size and
refused it where the output listed no such mode, so under Balanced on a 4K
output, 2259 × 1271, an X11 game stayed at full size; the proxy forwarded such
a connection unchanged and told it nothing. Both now ask for the listed mode
nearest to the wish (`nearestListedSize()` in `resolution.h`, the listing in
`x11modes.cpp`): of the modes the output lists, those of its shape that the
scaler enlarges, the nearest by width, the larger of two equally near, as the
Wayland scale answers a wish with the nearest reachable step. Xwayland's list
for a 4K output holds 2048 × 1152, which is nearer to 2259 × 1271 than
2560 × 1440 is, and so a Balanced X11 game now draws at 2048 × 1152 and is
enlarged. The status names the wish beside it ("the listed mode nearest to
2259 × 1271"), translated in the three catalogues. Only where no listed mode
qualifies is the request still refused. `asksTheNearestListedMode` replaces
`refusesUnavailableMode`, `answersUnlistedProgramsUnderAllApplications` asks the
proxy under Balanced, and `choosesTheNearestListedSize` tests the choice; the
proxy's case failed on the old policy, which told nothing. Step 4 is done;
what follows in the order needs KWin's answer (K15), new input mapping (K16,
K17, K21) or the hardware and machines of step 7.

K21, tested 2026-10-06, the first of step 5, K15 waiting for KWin's answer to
the proposal of 29b. The Wayland test client now binds the relative pointer,
can lock the pointer and open a popup, and the test driver moves its pointer
by a relative motion. `carriesRelativeMotionOntoThePicture`, under Fit (64 × 80
fitted into 102 × 128, bars left and right) and Integer (64 × 48 enlarged
twice, bars above and below): a relative motion reaches the game scaled to its
surface, and once the game holds the pointer locked only relative motion
arrives, scaled the same, the position held. It passed as the code stood and
fails with the filter's scaling of relative motion taken out.
`leavesAPopupOverTheBarsItsOwnPointer`: a popup the game opens at its surface's
(2, 40) lies there on the output, over the left bar, unenlarged, and the
pointer there is the popup's at (3, 3); the game hears nothing. That a popup
does not lie where the picture shows what it belongs to is stated in the
handbook. Subsurfaces are refused with the window (K12), so its input is
KWin's own; an overlay drawn into the game's buffer is part of its picture,
and one drawn as a window of its own is input of its own. Next in step 5 are
K16 and K17.

K1 and K2 are being addressed in the
[scale-one Auto work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29).
That section owns the implementation, real-display results and remaining
acceptance. It records both the cropped late-resize glmark2 case and the corrected
first-configure run; do not mark K2 removed from buffer dimensions alone. K15–K20 remain
open, and Jens excluded second-display checks from this session.
