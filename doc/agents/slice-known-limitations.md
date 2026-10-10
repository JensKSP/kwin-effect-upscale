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
| K1 | Wayland clients with different buffer-density policies | The [scale-one Auto work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29) measured them across desktop scales 1, 1.5 and 3: fractional-density clients follow the desktop scale, glmark2 the output's integer scale, low-density SDL and SuperTux one | A smaller configure reaches these clients at scale one; asked for two thirds at desktop scale 3, low-density SDL supplied 853 × 480, below half the output, which the status names as refused | F, stated | Stays stated, see Progress; a physical run at a scaled desktop is Jens's |
| K2 | Integer scale and startup-only sizing | vkmark ignores configure sizes; glmark2 keeps the viewport its first fullscreen configure gave it. The [scale-one Auto work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29) fixed the fresh glmark2 start with the first configure | vkmark uses the advertised mode; a fresh glmark2 uses a smaller first configure; a glmark2 resized later crops its scene, which the effect cannot see | F, stated | Stays stated, see Progress: a renderer's fixed viewport is the client's |
| K3 | Wine's Wayland driver | Removed for OpenGL 2026-10-07: Wine 10.0 itself, in the Debian 13 package machine, presented after two fixes; for Direct3D 9, 11 and 12 and Vulkan on 2026-10-10, borderless and exclusive, with Wine's own translation and software rendering; see Progress | The game enlarged from its subsurface's picture | F | DXVK, vkd3d-proton and real hardware go with K10 |
| K4 | A program that connected before it could be told | The Wayland mode is told when a client binds the output, the X11 screen when a connection goes through the proxy; a program already running, or an X11 program that does not connect through the proxy, hears neither | Full size, or an X11 program resized after it started showing part of its picture enlarged (glmark2 2023.01, SuperTux 0.6.3 keep their first viewport). For a Wayland program the status names the size "from the next start"; not checked for every route | S, stated | Tested per route 2026-10-06, see Progress; stays stated |
| K5 | A toolkit that picks another output | Extreme Tux Racer 0.8.4 with SFML 2.6.2 moves to the primary output when it recreates its fullscreen window | The shipped entry refuses control on a secondary output; the status says so | S, stated | Stays stated unless SFML changes |
| K6 | A requested X11 mode the program does not list | Removed 2026-10-06: the nearest listed mode is asked instead; see Progress | The game gets the listed mode nearest to the wish, enlarged | S | Done |
| K7 | Internal render targets that keep their size | SuperTux 0.6.3: the outer buffer and viewport went from 4K to 1080p, an intermediate framebuffer stayed 1368 × 769 | Nothing visible; the saving in GPU time is smaller than the buffer suggests | F | A measurement question for the cost matrix (item 74), not a defect; stated |
| K8 | A game that keeps the smaller size in its own settings | SuperTuxKart 1.4 writes the told mode into its configuration (item 17) | Under Native the effect enlarges the kept smaller buffer; under other presets the display marks the kept size in the warning colour | S, stated | Stated in the handbook's known limits since 2026-10-04; nothing to change in the effect |
| K9 | Programs in Flatpak and Snap | Removed 2026-10-07 for the shipped games: SuperTuxKart and Extreme Tux Racer from Flathub and the Snap Store enlarged on Wayland and through X11 in the Debian 13 (KWin 6.3.6) and Kubuntu 26.04 (KWin 6.6.6) package machines, see Progress | Enlarged as natively packaged games are; the Snap Store's Extreme Tux Racer stops at its own launcher's dialog before the game | F | Steam as Flatpak or Snap is item 34, in the [Proton slice](slice-proton-smaller-screen.md#remaining-work) |
| K10 | Wine and Proton across the graphics paths | Wine 10.0 with its own translation, WineD3D for Direct3D 9 and 11 and vkd3d for 12, and Vulkan, each borderless and exclusive on both of its display drivers, enlarged in the Debian 13 package machine on 2026-10-10 with software rendering, see Progress; official Proton, DXVK, vkd3d-proton and real hardware are item 36 | Unknown where not run | F | Item 36, in the [Proton slice](slice-proton-smaller-screen.md#remaining-work) |
| K11 | FreeBSD and the BSDs | FreeBSD's nightly installs, loads and removes the effect in an emptied machine; no session check, and the proxy is not run there (items 2f, 23) | Unknown | F | The session check once the virtio-gpu driver allows one |
| K22 | The desktop's own programs told the smaller screen under All applications | Removed 2026-10-07 by Jens's decision: the global profile is now All games and acts only for a program it recognizes as a game, at bind, at the proxy and for windows; an entry still applies to any program. In the Debian 13 package machine the panel spans the screen again; see Progress | Plasma's programs are told nothing and its splash screen is not announced; a game All games does not recognize is left alone, with the status saying so, which the handbook's known limits state | S | Done |

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
| K16 | Touch and tablet input | Touch mapped 2026-10-06, a Wayland window's pen 2026-10-07, see Progress; an X11 window's pen is not | Touch and a pen land where the picture shows them, one in a bar reaches nothing; an X11 game's pen applies in the window's unscaled coordinates | F | An X11 window's pen, once the X11 session has a tablet to test it with |
| K17 | Confinement regions and the locked pointer's position hint | The presentation section: not mapped | A confinement to part of a window, and a position hint on unlock, apply in the window's unscaled coordinates | F | Map both through the presentation, with K15 |
| K18 | The cursor over a hidden decoration | Removed 2026-10-06: KWin gave the hidden decoration the pointer and showed its cursor; both filters now take it away where they claim the pointer, see Progress | The game's cursor | S | Done |
| K19 | A visible dialog's title bar above a presented X11 game | Removed 2026-10-06: tested, the title bar keeps the pointer; see Progress | The dialog keeps its title bar | S | Done |
| K20 | Wine's Wayland driver's pointer | With K3 2026-10-06: the window is drawn over its output, and its pointer is mapped as any such window's | Mapped; a confinement meets K15 | F | With K15 |
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

K16, touch mapped 2026-10-06. KWin delivered a touch on a presented window in
the window's unscaled coordinates. Both filters now take a touch that goes down
on a picture and deliver it themselves, through `UpscaleTouchDelivery`
(`touchinput.{h,cpp}`): the seat moves a touch point by an offset alone, so the
offset is set at every down and motion to the one that lands the touch where
the picture shows it, and a touch in a bar is swallowed. Taking the touch passes
over KWin's own activation, so the filter activates the window itself. KWin
hands filters touch as arguments up to 6.3 and as event structures after it; a
configure probe, `UPSCALE_KWIN_TOUCH_EVENTS`, picks the overrides, and both
build against 6.3.6 and master. `TouchPoint::setSurfacePosition()` is not
exported to plugins built outside KWin, so its two public members are set
instead. The test driver gained a touch screen and the Wayland test client
`wl_touch`. `mapsTouchOntoThePicture` (a picture with bars: down, motion, a
touch in a bar reaching nothing) fails with KWin handling touch itself;
`drawsAWindowOfTheToldSizeOverItsOutput` touches beside a drawn window;
`mapsTouchOntoAPresentedGame` touches a presented X11 game, which the X server
makes a pointer press for a game that takes no touch: it lands a pixel short of
the halved point in Xwayland's own conversion, and at 300, 200 unmapped. After
a touch the X11 session's later pointer cases failed with or without the
mapping, until a pointer motion made the pointer the last input again, so that
case ends with one. A pen is not mapped: its tool events, focus and proximity
go through KWin's tablet filter and the tablet protocol, which differ again
between KWin's versions; it stays stated. K17 waits with K15.

K3 with K20, step 6, 2026-10-06. Wine's Wayland driver draws a game into one
subsurface covering its window, above the window's own buffer, and rounds the
subsurface's buffer height up to a multiple of 128, showing the told size of it
through a viewport; the effect refused both, the child surface and the crop. It
now takes the picture from that subsurface where it is the window's only child,
covers it whole at its origin and lies above it (`upscalePictureSurface()`), and
takes the picture's size from the viewport's source rather than the buffer
(`upscaleSuppliedSize()`), so that a Wayland client's crop in whole pixels is
captured as it stands; a crop in fractions of a pixel is still refused, and so
is any crop of an X11 window's buffer, which Xwayland makes for a mode it
emulates and presents itself. Allowing that too was the first attempt: the X11
suite's capture check stopped KWin at Xwayland's emulated mode. The capture
already rendered an item in its parent's coordinates and needed no other change.
`presentsThePictureOfItsOnlySubsurface`, a production case built like Wine's
driver (a 256 × 144 window of the told size with a black buffer, its picture a
256 × 160 subsurface buffer showing 256 × 144), was refused for its child
surfaces before and is now scaled from 256 × 144, the output's middle showing
the subsurface's pattern; all 19 production cases pass. The window is drawn
over its output, so its pointer is mapped as any such window's, which is K20's
half; a confinement meets K15. Wine itself was not run: no maintained image has
Wine, and the probe of item 35 stayed scratch. Running Wine 10's Wayland driver
is its full acceptance. Next in the order are K1 and K2, then step 7.

K1 and K2, stated 2026-10-06, which ends step 6. The density comparison K1
asked for is the table of the [scale-one Auto
work](slice-resolution-control.md#wayland-auto-at-scale-one-2026-09-29), across
desktop scales 1, 1.5 and 3. What remains of both is the clients' own policy: a
client that ignores density draws at most the logical screen, which no
configure enlarges; vkmark sizes its buffer from the mode at its start; and
glmark2 keeps the viewport of its first fullscreen configure, so a resize after
its start crops its scene, and a buffer of the right size alone never shows K2
removed. The effect names each refusal and the supplied size, and the
first-configure fix keeps a fresh glmark2 start whole; the handbook states both
under Auto's geometry request. The physical runs were at desktop scale one; one
at a scaled desktop is Jens's, and he excluded second-display checks from this
session. Next is step 7.

K3 with Wine itself, step 7, 2026-10-07, in the Debian 13 package machine:
KWin 6.3.6 on Mesa's llvmpipe, a 4K screen KWin runs at scale 1.05 of its own
accord, Debian's Wine 10.0 with the prefix's graphics driver set to `wayland`,
and a Windows OpenGL program built there with MinGW
(`tools/wine-opengl-probe.c`), a borderless window over its whole screen, red
on the left half and blue on the right, run by `package-vm.py debian-13-amd64
wine PACKAGE` under All applications. Wine heard 2560 × 1440, and the effect
refused it twice over; each was fixed and run again:

- Wine sizes a window in the Windows pixels of the screen it was told, one to
  a logical unit unless its own DPI setting says otherwise, whatever the
  output's scale, and keeps a fullscreen window at that size: 2560 × 1440
  logical, not the told mode's 2438 × 1371 at 1.05, and larger than the output
  on a desktop scaled twice or more. A fullscreen window at its output's corner
  whose picture is the told size is now drawn over the output (`88b0ef8`);
  `drawsAWindowOfTheToldPixelsOverItsOutput` sizes the test client so, at scale
  one and in the session at scale two, where it failed before. The test driver
  had drawn through a capture at scale one, which the effect hands back on a
  scaled desktop, so no case there had ever seen a picture drawn; it now
  captures at the output's scale.
- Wine draws an OpenGL game into a buffer with an alpha channel and declares
  no opaque region, over a window buffer it leaves transparent. Windows takes
  no alpha from a game's picture, and the effect draws a picture as it is over
  black, so the picture of a window's only subsurface is now taken whatever
  its alpha (`fdc67fb`); the production case has a row built that way, which
  failed before.

With both, the probe's 2560 × 1440 was enlarged with FSR 1 to 3840 × 2160, and
KWin's picture of the screen splits red from blue at its middle. K3 is removed
for OpenGL; Direct3D through wined3d and Vulkan on Wine's Wayland driver belong
to K10's matrix. Its pointer (K20) was not driven in the machine; it is mapped
as any drawn window's, as the session tests show.

The same run found K22. All applications was checked for the probe, which has
no entry, and at the next login Auto told plasmashell, Plasma's services and
the portals the smaller mode as they bound the output; the panel of that
session was laid out over two thirds of the screen's width, and the display
announced Plasma's splash screen as a window it would not scale. Which programs
the answer at bind reaches under All applications is Jens's to decide; the
handbook's known limits state it meanwhile.

K9, step 7, 2026-10-07, in the package machines with `package-vm.py SYSTEM
sandboxed PACKAGE`: SuperTuxKart and Extreme Tux Racer as the system packages
them, from Flathub (Flatpak 1.16.6) and from the Snap Store (snapd 2.77.1), on
Wayland and through X11, under their shipped entries, in the Debian 13 machine
(KWin 6.3.6, desktop scale 1.05) and the Kubuntu 26.04 one (KWin 6.6.6, scale
2.7). The runs found three defects of the effect, each fixed, tested in the
session suites and run again with packages of the fixed tree:

- On Kubuntu every X11 game was refused as "not fully opaque", depth-24
  windows included. KWin 6.6.6 makes an X11 window's opaque region from its
  shape rounded to whole logical units, and 2560 pixels at scale 2.7 are 948.1
  units; the effect now asks every whole unit to be opaque (`8879a94`).
- Extreme Tux Racer's entry asks the game to confirm a resize through the X11
  mode it sets. A game the proxy told the smaller screen starts at it and sets
  none, and the effect waited for it forever; the confirmation is now asked
  only of a game the proxy did not tell (`644311d`).
- The check's own faults, recorded in the
  [package machines](slice-package-machines.md#progress): a locked screen, the
  splash screen, a game left running between cases.

With `644311d`, on Kubuntu all eight cases of the two games that started were
enlarged from 2560 × 1440 with FSR 1, natively, as Flatpak and as Snap, on each
route. The effect named Flathub's SuperTuxKart on Wayland
`flatpak://net.supertuxkart.SuperTuxKart/app/bin/supertuxkart` (item 47 seen),
the proxy offered that name and `/app/bin/supertuxkart` for its X11 connection,
and the Snap is `/snap/supertuxkart/678/usr/bin/supertuxkart`, which the
entry's `.*/supertuxkart` takes. On Debian the same holds natively and for
Snap, and for Flatpak on Wayland; a Flatpak game through X11 is not enlarged
there. KWin 6.3.6 takes an X11 window's PID from the `_NET_WM_PID` its client
sets, which inside Flatpak's PID namespace names another process, as this
slice's entry predicted: SuperTuxKart's window is claimed by nothing and drawn
at 3840 × 2160, and Extreme Tux Racer's, claimed through its instance name, is
not known as served and waits for a mode. The proxy answered both connections
with the smaller screen. The next step is to trace an X11 window to its
connection by its ID: the proxy reads every connection's setup reply already,
and the resource-ID base and mask in it name the window IDs of that process.
The Snap Store's Extreme Tux Racer, an unproven publisher's build on core20,
stopped on both systems at a dialog of its own launcher before the game.

K9 on KWin 6.3, the same day. Tracing a window by its resource ID turned out
to be unnecessary: the proxy already keeps each connection's resource range
and its authenticated process, and corrects the XRes answers KWin 6.6 asks
for. KWin 6.3 reads `_NET_WM_PID` instead, which the client sets, so the proxy
now writes the connection's process into the `ChangeProperty` that sets it,
having learned the atom from the client's own `InternAtom` (`69f2223`; the
policy test fails without it). With a package of that tree, the Debian 13 run
had every case enlarged but the Snap Store's racer: Flathub's SuperTuxKart and
Extreme Tux Racer through X11 named `/app/bin/supertuxkart` and
`/app/bin/etr`, claimed by their entries and enlarged from 2560 × 1440. K9 is
removed for the shipped games; Steam in either form is item 34.

CodeRabbit's review of `f9ab2d2` found that a subsurface's picture taken
whatever its alpha was captured over black, where KWin shows the window's own
buffer through a clear picture. The scaler now captures the window's own
surface, which draws the subsurface over it as KWin composites them, at the
subsurface picture's size (`7df025e`); a production row with a picture clear
on its left half over a green window buffer found black there before and green
now.

K16's pen, 2026-10-07. KWin hands a pen to the window under it at the
window's own place, as it does the pointer. The picture filter now takes a
pen's proximity, motion and tip where a picture is, finds the window as for a
touch, maps the point and tells the client through KWin's tablet protocol what
KWin's forwarding would have told it, axes included; in a bar the pen reaches
nothing (`2847c6f`). KWin 6.6 gives the filters a pen's events as structs and
6.3 as a QTabletEvent, which a build probe tells apart; both build.
`mapsAPenOntoThePicture` brings KWin's virtual pen near the middle of a game
drawn over the 384 × 216 output and presses it at a quarter: the game hears
128, 72 and 64, 36, where KWin alone sent 72, 44. The protocol trace showed
KWin sending the mapped motion before the test client read it: the client's
tablet objects are on the test's own queue, which the case reads with a
roundtrip. An X11 window's pen stays unmapped: the X11 session has no tablet,
and code nobody tested was left out.

K22, decided by Jens on 2026-10-07: under the global profile only programs
recognized as games are told the smaller screen at bind or connection, and
an entry applies to whatever program it names. As the global profile then acts
for games only, it is called All games, and it acts for nothing else at any
point, windows and the display included: a fullscreen browser video is left
alone. A game is what Wine or Proton runs, a program in a Steam library, or
one an installed desktop entry in the Game category starts or its window
names (`gamerecognition.h`); the handbook's
[what All games reaches](../upscaling.md#what-all-games-reaches) states the
rules and their limits, which replace K22 in its known limits. The stored key
stays `UnlistedApplications`. Tested: the recognizer's own cases
(`upscale-gamerecognition`); `asksApplicationsForASmallerImage`, where the test
client with no Game entry is told the native mode under All games and an entry
still reaches it; `answersUnlistedGamesUnderAllGames`, where the proxy answers
a Steam library's program and the test's own once a Game entry starts it, and
refuses it before with "not in the list, and not recognized as a game". The
session tests, the production test and the conformance arms declare their own
client a game through such an entry (`autotests/game_entry.h`).

CodeRabbit's review of `ba1f857` found two faults, both fixed in the next
commit. A process an entry had answered counted as a game for good, so once
that entry was switched off All games took over its window: the served record
now keeps whether the program was a game when it was answered
(`upscale-served`). And All games still held every Wine prefix for a program no
process had named yet, as All applications had, against Jens's rule of
2026-10-03 that a pattern which could match in any prefix holds none; it holds
none now, and `answersUnlistedGamesUnderAllGames` checks that. Its review of
`8e6cae3` found that an entry rewritten in place keeps its directory's time,
so the index kept a stale answer: each entry's own time is now kept as well,
and `readsAnEntryRewrittenInPlace` failed on the old index and passes. As that
looks at every entry's time, a program is now recognized only while All games
acts, at bind too, where it had been asked first. Two stale handbook passages
it named were brought up to date as well.

K22 in the Debian 13 package machine, 2026-10-07, with a package of `b256cec`
and `package-vm.py debian-13-amd64 wine`, which logs in again with All games
checked: the panel spans the whole width of the 3840 × 2160 screen in the
desktop picture taken before the probe. The session's journal shows the
smaller mode told at bind to Wine's three `wine64` processes and to nothing
else, and every X11 connection of Plasma's own helpers - `kcminit`, `kded6`,
`ksmserver`, `xembedsniproxy`, `xsettingsd` and the rest - refused as "not in
the list, and not recognized as a game". The probe was enlarged from
2560 × 1440 to 3840 × 2160 with FSR 1, and KWin's picture splits red from
blue at its middle, as before.

K3 and K10 beyond OpenGL, 2026-10-10, in a fresh Debian 13 package machine
with a package of `0e0b334` and `package-vm.py debian-13-amd64 wine`. The
OpenGL probe became `tools/wine-probe/`, one Windows program with a part per
API: OpenGL, Direct3D 9 and 11 (Wine's WineD3D), Direct3D 12 (Wine's vkd3d)
and Vulkan (winevulkan on Mesa's lavapipe), each borderless, a popup window
over the screen, and exclusive, the API's own fullscreen or a display mode
set first. The check runs each on Wine's Wayland driver and on its X11 driver
through the session proxy, with a new Wine server per run, under All games.
All twenty were enlarged from 2560 × 1440 to 3840 × 2160 with FSR 1, on the
window system their driver speaks (`wayland-fullscreen` and
`x11-fullscreen`), and KWin's picture splits red from blue at its middle in
each. On the X11 driver the probe reported the 2560 × 1440 screen the proxy
told its prefix. The machine has no graphics card, so this shows what the
effect does with each path, not how a game runs on one: DXVK, vkd3d-proton,
official Proton and real hardware stay item 36.

What step 7 leaves: K10 and K13 on Jens's hardware, K11 once FreeBSD's
virtio-gpu allows a session, and item 34's Steam, which needs a Steam account.
