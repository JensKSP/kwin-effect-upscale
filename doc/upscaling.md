<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Upscaling developer handbook

This is the permanent developer handbook for the effect: requirements,
specification, design rationale and KWin integration constraints. Keep it in
sync with the implementation and distinguish intended behaviour from features
that already work. FSR 1 rendering, optional RCAS and the settings page are
implemented. Container rendering tests cover the shaders and configuration;
compositor lifecycle and real-game, HDR and VRR acceptance remain open.

This handbook and the source code, including comments and tests, are the
single source of truth. Temporary working documents for coding agents under
[`doc/agents/`](agents/) track bounded, unfinished work packages. Once a package
is implemented and all required tests pass, including real-device acceptance
where required, its working document is removed after lasting information has
been preserved here and beside the code. This handbook remains.

Statements about KWin below were read in the source at
`v6.3.6` (commit `b8de432`, the version Debian Trixie ships as `4:6.3.6-1`) and
at `master` (commit `c1ca390`, 2026-09-13).

## Product vision

The user experience must cover the complete gaming and upscaling workflow:
obtaining an appropriate game buffer, selecting suitable processing, presenting
the result correctly, and explaining the effective state through native KDE
settings and an on-screen display. The ideal first-run experience is to install
the matching Debian package and play, with no further setup required.

This is a product requirement, not a claim that the current prototype delivers
it. The implementation must work toward these outcomes:

- Installation and updates use the distribution's normal package tools.
  Packaging resolves dependencies and expresses KWin compatibility; users do
  not assemble a toolchain, reconcile conflicting library versions or edit
  configuration files to obtain the normal gaming experience.
- Settings, shortcuts, notifications and the on-screen display follow KDE
  conventions. The ordinary path offers a small set of understandable choices;
  advanced tuning remains optional, and diagnostics explain actual behavior.
- A comprehensive, maintained catalogue of well-known games supplies tested
  profiles and recommended settings. Recommendations account for the game,
  hardware and display rather than presenting one preset as universally
  optimal. Users retain explicit overrides and can see what was selected.
- Game matching, resolution handling and upscaling cooperate as one
  experience. Supported games should not require users to compose launch
  commands or discover a sequence of unrelated workarounds. Unsupported cases
  must be identified clearly, without pretending that a requested setting took
  effect.

The requirements for application profiles, in-session resolution control,
settings and validation specify the pieces of this experience.
Installing a package, loading the plugin or passing CI alone does not establish
that the vision has been achieved; acceptance must exercise the complete user
journey on supported hardware.

## The gap

A game that renders below the resolution of the screen it covers has to be
enlarged by someone. On Windows that someone is the graphics driver: AMD calls
it RSR, NVIDIA calls it NIS, and the compositor is not involved. On Linux
`amdgpu` and Mesa have no equivalent. What is left is:

| Where | What happens |
| --- | --- |
| gamescope | It is a compositor of its own. The game gets a virtual screen of the inner size (`-w/-h`) and the finished image is scaled to the output (`-W/-H`). Current master offers linear, nearest, pixel, FSR 1, NIS and SGSR filters; integer scaling is a separate geometry setting. |
| Proton-GE | `WINE_FULLSCREEN_FSR=1` offers smaller resolutions to the game and scales them with FSR 1. |
| A plain KDE session | Nothing. Steam is an ordinary window, and the image is stretched with whatever filtering the compositor happens to use. |

In a plain session that filtering is bilinear (`GL_LINEAR`, e.g.
`src/opengl/glframebuffer.h`) or the hardware plane doing it (`SRC_W`/`CRTC_W`,
`src/backends/drm/drm_pipeline.cpp:265-274`). Neither KWin 6.3.6 nor master
carries FSR or NIS.

## What this effect does

The initial rendering path scales the buffer supplied by the game. Per-game
resolution control is implemented for cooperating native Wayland and X11
clients. It requests a smaller buffer without patching KWin, then enlarges that
buffer on the physical output. Compatibility is method- and client-dependent.

**The primary platform is a KWin Wayland session. Native Wayland games and
Xwayland games must be supported, including Windows games running through
Valve's Proton and through standalone Wine.** Xwayland
compatibility is required within the Wayland session; it does not imply a
requirement for a separate X11 desktop session. Resolution-control mechanisms
may differ between the two client types and require separate validation.

The effect acts on a window when all of this holds:

- the window is fullscreen, or a profiled normal undecorated window exactly
  covering its output, and
- its buffer is smaller than the output area it covers, and
- the effect is enabled, the application's rule permits scaling, and the output
  exceeds its configured pixel threshold.

Everything else uses normal KWin rendering. A native-size buffer is not scaled.
Resolution control changes only the selected client's output information or
X11 geometry; the shared desktop output mode remains unchanged.

### Four requirements that bound every route

Laid down by Jens, 2026-09-19. These govern this whole document. Where an
investigation, a proposed method or a recorded experiment conflicts with them,
they win and it is out of scope, however well it worked. Together they are one
product statement: the user installs the package, ticks the effect once, starts
a game the way they always have, and it works.

1. **The plugin is self-sufficient.** It works with an unmodified supported
   KWin and an unmodified game: no KWin patch, no game patch, no modified
   runtime, and nothing the user has to install beside it to make a game
   render smaller.
2. **The user starts the game normally.** From Steam, from a desktop file,
   from a shell, however they already do it. The effect acts only as a KWin
   plugin, from inside the session the game happens to start in. It does not
   wrap, relaunch, interpose itself in, or require anything of the command
   that starts the game.
3. **Installing the package is the whole of the setup.** The user installs the
   Debian package and, while the effect ships disabled, ticks it once in System
   Settings. Nothing else: no configuration file to write, no environment
   variable to set, no external tool to install, no per-game preparation. The
   application profiles ship inside the package and are updated by it; a user's
   own entries are an option they may take, never a step they must take.
4. **The plugin affects only what it is configured to act on.** A program the
   effect has not been configured to manipulate renders exactly as it would
   with the effect uninstalled: same resolution, same screen information, same
   window. Reaching one program by changing something shared with others is
   not an implementation detail to weigh against convenience; it is the thing
   this requirement forbids. A mechanism whose reach is wider than its target
   is unusable however well it performs, and being brief does not narrow it:
   a change that lasts only while a game runs still reached everything else
   while it lasted.

**What they cost.** They rule out every launch-time route, including the two
that were measured to work: the Sommelier protocol proxy and the Gamescope
Wayland backend both supplied smaller original buffers to an unmodified KWin,
and both require the game to be started through them. The launcher adapters,
the private virtual desktop and the per-profile launch helper go with them.
Those results stay recorded as mechanisms that exist; they are not routes this
effect may take.

**Sommelier is still worth reading, as a source of technique rather than a
route.** It solves, in a proxy, several of the problems this effect has from
inside KWin: how to present one client with a smaller output than the real one,
how to map absolute pointer positions between the advertised size and the
physical one, and what a client does when it is told a size. One result is
directly relevant. Unmodified Sommelier failed on Qt because its direct-scale
mode advertised a smaller screen while fractional-scale hints asked for double
density, and Qt went on supplying 4K; the prototype fixed it by hiding the
fractional-scale global so the client rendered at scale one and covered the
output. That is the same collision seen on a fractionally scaled desktop here,
where a client sizes its window from the mode it was told while the output's
logical size is divided by the desktop scale, and the window then does not
cover its output. Read it for that, and for what it had to do about input
mapping and buffer forwarding. Do not read it as a shape to adopt: every
version of it starts the game.

**What is left.** Telling one recognized native Wayland client that its screen
has a smaller current mode, which needs no helper and no restart and is
implemented. Resizing one selected X11 window so that an application which
handles resize asks for the smaller mode itself, now integrated with bounded
validation and restoration. The game's own settings remain an optional route.
Anything else the compositor already offers a plugin, for one window at a time.

**What it means for Xwayland.** The direct routes are closed and an indirect
one is open. Read against Xwayland 24.1.6, the version Trixie ships:

- Xwayland has per-X-client resolution emulation, at exactly the granularity
  the fourth requirement asks for: it records an emulated mode per X client and
  uses a viewport to present the smaller buffer at output size, which is the
  shape this effect needs. The compositor cannot set it.
  `xwl_output_set_emulated_mode` has two call sites, the RandR CRTC handler and
  the XF86VidMode handler, and both pass `GetCurrentClient()`, so only the
  client that asks is placed in a smaller mode. Writing
  `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` is not a way around it either: that
  property reports server state rather than establishing the mode.
- Reaching the game through the `wl_output` given to the Xwayland connection is
  what the fourth requirement forbids, because every X11 client shares that
  connection, and confining it to the minutes a game runs does not make it
  narrower.
- **What is open is making the game ask.** The effect can resize the selected
  X window, and an application that handles resize properly responds by
  reselecting its fullscreen mode on its own connection — which is exactly the
  request the compositor may not make for it. Xwayland's per-client emulation
  then applies to that client alone and its viewport presents the smaller
  buffer at output size. This has been demonstrated on KWin 6.3.6, so it needs
  no newer compositor, and it leaves other X11 clients untouched. It is a
  compatibility mechanism rather than enforcement: an application whose event
  loop ignores resize keeps its size, and a smaller drawable alone does not
  prove the application reduced what it renders into it. The resolution slice
  records the demonstrated sequence and its limits.
- A newer KWin opens a second, simpler case: a game whose resolution the user
  chose in its own settings. KWin gained `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` handling in `bc5a2002e9`,
  "x11window: support xrandr emulation", first tagged `v6.5.90` and so shipping
  in Plasma 6.6; `X11Window::configure()` reads the property and sizes a
  fullscreen X window to the emulated size instead of the output size, which is
  the one condition Xwayland's viewport path was missing. On such a KWin the
  whole chain closes, and it is worth stating end to end because every link is
  now read from source rather than assumed:

  1. The game asks for a mode. SFML calls `XRRSetCrtcConfig`, SDL and OGRE do
     the equivalent; Xwayland's own comment names those three as the libraries
     this path is for.
  2. Xwayland records an emulated mode against that X client and sets
     `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` on its windows.
  3. KWin 6.6 or later sizes the fullscreen window to the emulated size.
  4. Xwayland's viewport condition matches, so it sets the viewport source to
     the emulated size and its destination to the output size.
  5. The effect sees a smaller buffer with a full-output destination, which is
     exactly what its eligibility rules require, and scales it.

  Step 1 happens there because the user chose a resolution in the game, so this
  is the game-settings route working on Xwayland. On KWin 6.3.6 that chain
  breaks at step 3, which is why the resize mechanism above matters: it reaches
  the same viewport without needing the property at all.

So Xwayland is not closed to this effect, but nothing about it is free. The
resize mechanism depends on the application handling resize, the game-settings
route depends on the compositor version, and neither reduces what an
application chooses to render into the buffer it was given. Report what was
asked, what the drawable became and what the application committed as three
separate observations, and never present one as the others.

### Clients that are not games

The eligibility rules describe buffers, not applications, so any fullscreen
client committing a buffer smaller than the output it covers is processed by
the same code. Several non-game clients routinely do this: virtual machine
consoles, where the resolution is chosen inside the guest; remote desktop,
thin-client and game-streaming viewers, where it is negotiated with the remote
side; and players and emulators presenting a fixed-size image.

Whether a particular client qualifies is a question about its committed buffer,
its opacity and its surface tree, and has to be measured rather than assumed.
Resolution control does not apply to these clients at all: their size is
decided by a guest, a remote session or a file, not by a display query this
project can answer. Treat them as beneficiaries of the rendering path and as
candidates for recommended application profiles. They are a reason to keep the
rendering path free of game-specific assumptions, not a separate feature and
not a claim of support before one of them has been measured.

### Borderless windows at the size of the screen

Many games offer borderless windowed operation instead of exclusive fullscreen,
and both native Wayland and X11 borderless clients are required. The implemented
path accepts a selected normal undecorated window only when its client and frame
geometry exactly cover one output, including the origin. Panels, wallpapers,
ordinary smaller windows and spanning windows are excluded. A supplied buffer
must still cover the entire logical destination before FSR can replace normal
rendering.

The [borderless implementation](#borderless-windows-and-gamescopes-approach)
uses the existing client protocols to retain that destination. A cooperating
X11 client can respond to a resize with its own per-client RandR request; a
Wayland client must retain a full-output surface through its scale or viewport.
Arbitrarily stretching a smaller ordinary window, with the accompanying input,
confinement and stacking changes, remains unsupported. Physical pointer and
output-movement acceptance remains required for the implemented paths too.

Reduction is a request: a client may ignore it, or commit a smaller buffer while
keeping larger internal render targets. Report the request, resulting window
and supplied buffer separately. Applications without a cooperating presentation
path remain an open compatibility requirement, not a reason to require users
to change their launch command or configure their game.

## What it does not do

- **No frame generation.** Frames are scaled, never invented.
- **No temporal upscaling.** FSR 2/3/4, XeSS, DLSS, Arm ASR and SGSR 2 need
  motion vectors from inside the game. A compositor does not have them, and
  faking them would produce artefacts the game cannot correct for.
- **No replacement for in-game upscalers.** A game that ships FSR 2 or DLSS
  should use it; it has data this effect will never see.
- **No KWin patches.** Resolution control and upscaling must work with an
  unmodified supported KWin. Calling exported KWin APIs from the C++ plugin is
  allowed; requiring a patched compositor is not.

## Windows games: Proton and Wine

**Upscaling through Valve's Proton and standalone Wine is mandatory for the
initial usable implementation.** It must work with eligible smaller game
buffers on every supported KWin version, including 6.3.6. A game launching
successfully, ordinary KWin stretching, or passing only the native Linux tests
does not establish compatibility. The effect must actually process the smaller
buffer, with the same colour, presentation, input and lifecycle requirements.

Use Valve's Steam-distributed Proton as the baseline and validate standalone
Wine separately. Proton-GE may be an additional test, but must not be the only
working route or a prerequisite. Proton itself uses Wine, as described in
[Valve's Proton documentation](https://github.com/ValveSoftware/Proton), but that
does not make the two runtime configurations interchangeable test results.
Games obtained through Steam and Epic Games Store belong in the later game
acceptance stage; record their actual runtime and launcher independently of
the store.

The Windows test matrix must cover OpenGL, Vulkan and Direct3D translation
paths. Include the OpenGL-based WineD3D path, Direct3D 9/10/11 through
[DXVK](https://github.com/doitsujin/dxvk), and Direct3D 12 through
[vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton), with suitable
representative applications. Test Windows Vulkan applications through the
runtime as well. Record the application API, translation layer and version,
Proton or Wine version, and actual window-system backend. Xwayland is required;
test native Wayland drivers where the selected runtime supports them, without
assuming that choosing Vulkan also chooses Wayland.

Keep the physical output at its native mode. In-game, compositor, runtime and
driver upscalers other than this effect must be disabled for the baseline
comparison so the result is
attributable to this effect. A smaller internal game render resolution that
still produces a native-size submitted buffer is a bypass case. Resolution
control remains subject to the requirements above; inability to obtain a
smaller buffer is an unresolved case, not a passed upscaling test. HDR and VRR
remain required when supported by the game, runtime and output path.

## Scalers

Only spatial scalers can be used, for the reason above. Candidates, with the
licence that decides whether the shader may be carried here at all:

| Scaler | Kind | Licence | Usable |
| --- | --- | --- | --- |
| AMD FSR 1 (EASU + RCAS) | upscale + sharpen | MIT | yes |
| NVIDIA Image Scaling (NIS) | upscale + sharpen | MIT | yes |
| Snapdragon GSR 1 | single pass, Lanczos-like 12-tap + adaptive sharpen | BSD-3-Clause | yes |
| Lanczos, bicubic (libplacebo implementations) | classic | LGPL-2.1-or-later; retain additional source notices | candidates |
| Nearest / integer scaling | pixel replication / geometry | own implementation | yes |
| xBRZ (KWin zoom shader) | pixel-art reconstruction | GPL-2.0-or-later | candidate |
| AMD CAS | sharpen with optional upscale | MIT | candidate |
| MAKO Scaler | single pass | GPL-3.0-or-later | **no** |

MAKO is ruled out as a source of code, not on quality: this project is
`GPL-2.0-or-later`, and taking GPL-3 code would pin it to GPL-3 and rule out a
merge into KWin, whose effects are `GPL-2.0-or-later`. Third-party shaders that
are used keep their own licence headers; see `LICENSES/`.

For reference, KWin master already carries an xBRZ upscaler in the zoom effect
(`src/plugins/zoom/shaders/upscaler.frag`).

Filter choice and image geometry are separate: nearest sampling does not by
itself guarantee an integer scale factor. The implementation, not the name of
the algorithm, determines the licence for copied code.

[CAS's reference header](https://github.com/GPUOpen-Effects/FidelityFX-CAS/blob/9fabcc9a2c45f958aff55ddfda337e74ef894b7f/ffx-cas/ffx_cas.h)
documents linear-light input and a maximum fourfold increase in pixel count
for its scaling path. It is not interchangeable with RCAS, which only sharpens.
[Gamescope's reviewed renderer](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/rendervulkan.cpp)
runs its modified SGSR filter followed by RCAS; that is a different pipeline
from Qualcomm's single-pass reference. Neither pipeline establishes HDR
correctness or performance for this KWin effect without separate validation.

### Aspect ratio and integer scaling

Required extension, not yet implemented: support fullscreen content whose
aspect ratio differs from the output, and provide integer scaling for pixel
art and older games. Add global settings with sparse application overrides.
This expands the initial geometry restrictions; it does not imply that the
current FSR path handles these cases.

- **Fit, preserve aspect ratio:** enlarge the complete supplied image as far as
  the selected filter permits without stretching or cropping it. Centre it and
  fill the remaining output area with black bars. For example, 1440 × 1080
  content on a 3840 × 2160 output occupies 2880 × 2160, with 480-pixel bars on
  each side. Fit is the default geometry when this extension is available.
- **Integer:** use the largest positive whole-number factor that fits both
  dimensions, centre the result and leave black bars where needed. Combine it
  with **Nearest neighbour** filtering for exact pixel replication, with
  sharpening off. For example, 320 × 240 becomes 2880 × 2160 at 9× on a 4K
  output. Expose geometry and filtering separately; nearest filtering alone
  must not be labelled integer scaling.

Calculate the destination in physical pixels, independently of desktop scale.
Permit at most the unavoidable one-pixel imbalance between opposite bars when
centering. If no positive integer factor fits, report that integer scaling is
unavailable and retain normal rendering; do not silently downscale or crop.
A factor of one is valid centred presentation without enlargement. The wider
integer range belongs to the nearest-neighbour path; retain FSR's supported
scale limits and report a filter/geometry combination that cannot be honoured.
Never apply sharpening to bars or filter across the image boundary.

Preserve the complete image and the physical output mode. A buffer that already
contains letterboxing is treated as supplied; automatic bar detection or
cropping is outside this requirement. Geometry changes must also preserve
absolute pointer mapping, relative motion, confinement, locking, popups and
separate overlays. Keep this input and surface-tree work explicit rather than
assuming a different draw rectangle alone implements the feature. Rendering,
HDR/VRR and real-game acceptance are tracked in the
[geometry slice](agents/slice-scaling-geometry.md).

## Processing modes

Proposed extensions, not decided and not implemented. The implemented path has
a single mode: it enlarges a smaller supplied buffer with EASU and optional
RCAS. Two further modes would extend what the effect does with a finished
image without changing its compositor-side nature. Neither invents frames or
recovers detail the client never rendered, and both remain subject to the
existing eligibility, colour, HDR, VRR, damage and lifecycle requirements.

A mode decides what happens to the pixels. It is separate from the geometry and
filter choices specified under aspect ratio and integer scaling above, which
decide where the result is drawn and how it is sampled.

| Mode | Supplied buffer | Processing |
| --- | --- | --- |
| Upscale | smaller than the destination | EASU with optional RCAS; the implemented path |
| Sharpen only | equal to the destination | sharpening at native size, no enlargement |
| Supersample | larger than the destination | filtered reduction to the destination |

A mode is an explicit user choice, not a second enable switch, and follows the
same global-default and sparse per-application override model as other
settings. A mode that does not apply to the buffer that actually arrived must
say so and fall back to the configured behaviour; it must never silently apply
a different mode. Each mode needs its own rendered-pixel tests and its own
status text, because a selected mode is not evidence that it ran. Adopting any
of them means opening a slice document first, as for any other major slice.

### Sharpen only at native resolution

The implemented path bypasses both filters when the supplied buffer already
matches the destination. **Sharpen only** would keep that bypass as the default
and add an explicit mode that runs the sharpening pass alone on a native-size
buffer. It addresses games that render at native resolution, games whose own
temporal upscaler already produced a native-size image, and content that is
simply soft. It is the one mode that is useful to a user who never lowers a
game's resolution at all.

- The mode never enlarges. A buffer smaller than the destination is outside
  its scope; report that rather than quietly upscaling.
- Sharpening keeps its existing scale and its real zero bypass. At strength
  zero the frame is unchanged, and the effect must then become inactive and
  release its scanout block rather than compose an identical frame.
- Every frame pays for lost direct scanout in exchange for a filter whose
  benefit is a matter of taste. Measure that cost separately from the upscaling
  measurements, in the same A0/A1 form, before offering the mode as useful.
- RCAS is the implemented sharpener. CAS remains a candidate with different
  input expectations, as the scaler section records; the two are not
  interchangeable, and a mode selector must not imply that they are.

### Supersampling a larger buffer

`canUpscale` rejects a buffer larger than its destination, so a client that
renders above the output resolution is reduced by KWin's ordinary filtering.
**Supersample** would add a deliberate reduction pass: the mirror of the
upscaling path, and the compositor-side half of what AMD calls VSR and NVIDIA
DSR. It trades frame rate for image quality rather than the other way round,
which must be stated plainly wherever it is offered.

- The effect owns only the reduction. Obtaining a larger buffer is the same
  unresolved resolution-control problem as obtaining a smaller one, with the
  added difficulty that a client will not render above its fullscreen size
  unless something tells it a larger size exists. Until a control path is
  verified, the mode applies to buffers that arrive larger for the client's own
  reasons, and the desired resolution stays guidance.
- Reduction needs a filter suited to minification. Bilinear sampling of a
  buffer more than twice the destination per axis discards samples and aliases;
  use a box filter at integer factors and a windowed filter otherwise, with the
  sample pattern documented rather than left to the driver.
- Average in a domain where averaging is meaningful. Reducing in an encoded
  transfer function shifts edge brightness; the existing decode into the
  bounded working domain applies here for the same reason it applies to EASU.
- The desired-resolution range extends above 100% only in this mode, with its
  own ceiling. A percentage above 100% must never be readable as a request for
  a smaller buffer, and switching modes must not reinterpret a stored value.

## HDR and variable refresh rate

HDR and VRR are required, including simultaneous HDR upscaling and VRR.
The effect must preserve KWin's colour management and adaptive presentation
when the client, output and driver support them. SDR content on an HDR output,
HDR content using PQ or scRGB, and transitions between SDR and HDR belong in
the acceptance tests. Intermediate formats and colour conversions must retain
HDR range and sufficient precision.

KWin remains responsible for presentation timing. The effect must not impose
fixed-rate presentation or continuous repainting merely to run the scaler.
VRR must be verified while upscaling is active and direct scanout is blocked.
A configured VRR option or a successful test with the effect disabled does
not establish this. Display-link limitations are recorded separately and
leave the corresponding hardware test pending.

## Selected initial approach

The initial implementation uses **FSR 1: EASU with optional RCAS**, starting
with 1080p and 1440p fullscreen content on a 4K output, supporting SDR, HDR
and VRR. This choice provides a documented reference and a comparable
gamescope path; it is not a measured quality or performance ranking of the
candidates.

The initial path targets opaque RGB surfaces with known colour descriptions,
their full buffer visible, an unrotated output, matching aspect
ratios and enlargement of at most two times per axis. Source and destination
sizes are physical pixels. Unsupported cases use KWin's normal rendering.

The first prerequisites are access to the original buffer in KWin 6.3.6,
a defined HDR colour path for EASU/RCAS and VRR during active composition.
FSR 1 remains the selected starting point, subject to these feasibility
checks. A failure requires revisiting the integration or scaler choice,
without dropping HDR or VRR from the requirements.
EASU must replace the enlargement step, and RCAS must be independently
switchable, initially off. The implementation uses FP32 fragment
shaders through KWin's OpenGL abstractions, with GLSL ES support checked.
Acceptance requires comparison with ordinary KWin scaling, GPU timing of the
complete rendering path and native tests with a real game on the TV, including
HDR and VRR together. An SDR-only prototype is an intermediate development
step. Falling back to ordinary KWin rendering for HDR does not satisfy HDR
upscaling support.

## Where the effect has to hook into KWin

- **Getting the surface at buffer size.** `EffectWindow::windowItem()`
  (`src/effect/effectwindow.h:672`) leads to `SurfaceItem`, which exposes
  `bufferSize()`, `bufferSourceBox()` and `destinationSize()`
  (`src/scene/surfaceitem.h:29-38`). The usual offscreen route hands the window
  over at target size, which is one scaling step too late.
- **Preventing direct scanout**, or the compositor hands the buffer to the
  display hardware and never calls the effect at all:
  `Effect::blocksDirectScanout()` (`src/effect/effect.h:902`).
- **Wayland clients that scale themselves** through `wp_viewporter`
  (`src/wayland/surface.cpp`, `viewport.sourceGeometry`, `destinationSize`)
  must not be scaled twice.

### Implemented render path

The effect captures the eligible surface item at buffer resolution through
KWin's item renderer. This keeps KWin's texture import and synchronization
handling. It does not use `OffscreenEffect`, whose 6.3.6 implementation captures
an already enlarged, 8-bit sRGB window. Separate windows and cursors continue
through the ordinary effect chain; surfaces with child items are initially
unsupported rather than scaling their contents together.

Capture and optional EASU intermediates use RGB10_A2 for non-linear destination
transfers (sRGB, gamma 2.2 and PQ), filtering directly in that encoding. Linear
destinations use RGBA32F to retain signed and extended-range values. KWin’s
GLES allocator can create 8-bit storage regardless of the requested format; the
compatibility layer replaces that storage and checks allocation and framebuffer
completeness. Shader samplers use high precision on GLES. Allocation failure
disables processing until reconfiguration and renders normally.

Capture preserves the original target colour description, so KWin performs its
normal gamut conversion and tone mapping once, at input resolution. For linear
destinations, EASU maps values in units of reference white to
`0.5 + 0.5 * sign(c) * sqrt(abs(c)/(1+abs(c)))`. RCAS uses the same bounded
domain, and the inverse restores signed linear values. Non-linear destinations
use specialized shaders without that conversion. Colour regression tests cover
these paths; HDR image quality still needs physical-display acceptance.

Supported destination transfers are sRGB, gamma 2.2, linear and PQ. New or
unknown destination transfer functions use normal rendering. There is no frame
timer: client damage expands to a full-window repaint because both filters
sample neighbouring pixels. The effect blocks scanout while it has an eligible
candidate or a visible diagnostic overlay; this effect API exposes a
session-wide scanout veto. Window selection and resolution policy still apply
independently per output. KWin retains ownership of refresh and presentation
timing.

## Versions

| | Version | Role |
| --- | --- | --- |
| Minimum | KWin 6.3.6, effect API `0.236` | what Debian Trixie ships; the supported target |
| Packaged | KWin 6.6, from Kubuntu 26.04 LTS | the Ubuntu package target |
| Tracked | KWin git master | built in CI to catch API changes early, not a supported target |

KWin 6.6 uses its own regions and shared colour descriptions. The later
render-device API also changes paint callbacks, EGL construction and shader
validation. These are separate compatibility boundaries: the presence of
`core/region.h` does not imply `core/renderdevice.h`. The compatibility layer
keeps these differences out of the scaling and colour logic, and tests exercise
both stable versions and the tracked development version.

## Configuration

### About, build identity and third-party notices

Implemented so far: the settings page names the installed build with its
version, branch or tag and build time, and names the build the running
compositor answers with when that differs, because KWin keeps a plugin it has
already loaded until the session restarts. The effect writes the same identity
to the log once, when it initializes, rather than when its library is loaded,
so a process that only reads the identity does not claim to have loaded the
effect. The full About dialog, the plugin metadata below and the component
notices remain unimplemented.

Required extension, not yet implemented: provide **About Upscale** from the
effect's settings using KDE's standard About presentation. Prefer the host's
About action if it can show the effect's own complete data. Otherwise add a
small standard About/information button in the settings page, with an accessible
name, tooltip and keyboard access. Use a KDE About dialog, such as
[KAboutPluginDialog](https://api.kde.org/kaboutplugindialog.html), rather than
replacing the settings host's application-wide About data. A linked details
dialog or tab may hold the additional build and component information.

The settings entry must work without a running game or active upscaling.
Opening or closing About must not apply settings, start the effect or change
the module's unsaved state. Information is selectable and copyable, with a
**Copy build information** action for reporting a particular build.

The current generator retains the timestamp of an unchanged source/build
identity and records a short revision in snapshot versions. The fresh timestamp
on every invocation, independent full hash, archive identity and complete
About/notices record below remain requirements, not implemented claims.

#### Required identity fields

| Field | Content |
| --- | --- |
| Plugin name | Upscale, with project identifier `kwin-effect-upscale` where useful. |
| Author | Jens Koehler, from the project's maintained author metadata. Preserve additional contributor and third-party credits separately. |
| Version | The exact compiled version, including package/snapshot suffix and dirty state where applicable; derive it from the existing single version definition. |
| Project | Clickable [GitHub project](https://github.com/JensKSP/kwin-effect-upscale) link. |
| Branch/tag | Label the build's branch or tag accurately; show both when known. A tag is not a branch and detached HEAD must not be described as master. |
| Git revision | Full commit hash as an independent field, including release builds whose version has no hash suffix. A shortened display may offer the full value for copying. |
| Build date and time | Complete ISO 8601 UTC timestamp, with the timezone visible. Honour `SOURCE_DATE_EPOCH`; identify a reproducible timestamp as such instead of claiming it is a measured wall-clock compilation time. |
| License | `GPL-2.0-or-later`, a clickable [license link](https://github.com/JensKSP/kwin-effect-upscale/blob/master/LICENSES/GPL-2.0-or-later.txt), and access to the bundled full text. |

Use one consistent identity record for the settings dialog, startup log and
optional overlay. Run the generator on **every build invocation**, including
builds without source changes and direct builds of the effect or settings
target. Recompute revision, ref and the complete timestamp then, not only during
configuration or after a new commit. Read the timestamp through CMake with
`SOURCE_DATE_EPOCH` support; do not retain an earlier wall-clock build time just
because the sources are unchanged.

Put changing values in one small generated `.cpp` behind stable declarations.
Replace it only when the generated content differs. A changed timestamp or
revision must require only that small unit to compile and the affected binaries
to link; it must not rebuild the scaler, settings UI or other consumers. An
invocation with identical resulting values, including a fixed reproducible
timestamp, should write nothing and need no compilation. Keep volatile values
out of headers, embedded plugin JSON and Qt resource inputs, and verify that
they do not trigger metadata/resource regeneration. Standard plugin metadata
can hold stable identity and the base version; the exact build record augments
the About presentation at runtime.

Never query Git from the installed plugin. Keep project-specific generation
outside the copyable plugin folder. Preserve known source revision/ref metadata
when producing source archives. For arbitrary archives without that information,
show **Unavailable**
or **Unknown** explicitly; do not infer a full commit or branch from a version.

Show which binary a record describes. If a newly installed settings module and
the effect already loaded in KWin differ, distinguish their records and indicate
that the loaded build remains older until reloaded. Never silently present the
installed build as the one currently rendering. If loaded-build information
cannot be obtained, label that state and retain access to installed information.

#### Startup log

Emit the complete identity above at information level when the effect is
initialized, including initialization during KWin startup and explicit loading
later. One concise record or small labelled block per initialization is enough;
do not repeat it on frames, reconfiguration, dialog opening or game detection.
This extends the current version/branch/date/Qt announcement. Include the
project and license URLs and where the installed third-party notices can be
read; full license texts do not need to fill the startup log. A settings-only
metadata inspection must not announce that the compositor effect was started.

#### Third-party components and licenses

Provide **Third-party components and licenses…** from About, either in the same
dialog or a linked, searchable details dialog. Include the libraries, shaders
and other dependencies used by the selected build, distinguishing bundled or
adapted code, linked runtime libraries and build-only tools. Identify relevant
transitive components when their code or notices are included in the delivered
artifacts. Do not list merely considered scalers as incorporated components.

For each component provide its name, purpose, version or source revision where
known, authors and copyright holders as supplied upstream, copyright notices,
applicable license expression and exceptions, upstream/source link, and access
to its full license and required notice texts. Preserve modification notices
where applicable. Retain complete upstream attribution rather than replacing
it with a project name or inventing individual authors from a commit log.
When build-time and runtime library versions are both shown, label them.

Review the licenses of the actual files/modules and how they are distributed;
do not assign every Qt or KDE component a single assumed license, and preserve
`AND`, `OR`, `WITH` and “or later” distinctions. Keep an audited notices inventory
in sync with SPDX headers, `LICENSES/`, package copyright information and actual
build inputs. It describes attribution, not a second build-dependency list.
Unresolved licensing or missing required notices must be corrected before a
release is described as complete.

Required attribution and license texts must be accessible offline from the
installed package as well as through the settings UI. External links supplement
those texts; they are not the only access route. Package the notices even when
the settings module is omitted. The current EASU and RCAS sources retain
Advanced Micro Devices, Inc.'s 2021 copyright and MIT notices; include these
explicitly in the viewer and package metadata. The
[MIT terms](https://spdx.org/licenses/MIT.html) require preservation of the
copyright and permission notice, so a generic license name or author list alone
is insufficient for these incorporated shaders.

The About UI provides access to notices; it does not by itself satisfy every
distribution obligation. The release process must preserve required notices
and provide corresponding source or other materials under the applicable
licenses. For example, [GPL version 2, sections 1–3](https://spdx.org/licenses/GPL-2.0-or-later.html)
sets notice and source-distribution conditions. Verify the chosen delivery
route and link the matching released source where available; a link to the
latest development branch is not an exact source record for an older binary.

#### Optional overlay access

An About view in the shared in-game overlay is optional and does not gate the
settings/logging implementation. If provided, reuse the same identity and
component notices, offer access to the full details, and follow the overlay's
focus, capture and cleanup rules. Keep it separate from the game-detection
announcement and passive statistics; displaying a startup About overlay is
not required.

Implementation and remaining acceptance are tracked in the
[development infrastructure slice](agents/slice-development-infrastructure.md),
together with diagnostic logging and the passive OSD.

### Diagnostic logging and state

About, logs, settings diagnostics and the developer overlay must describe the
same loaded build and observed effect state. Keep build identity separate from
changing runtime state. Offer a copyable diagnostic snapshot in settings,
including explicit unavailable values when the effect is not loaded. Do not
load the effect just to inspect it.

Implemented: the effect builds one snapshot of its current state in a single
pass, and the settings status text and the on-screen display are both formatted
from it, so they cannot describe different moments. Values the effect cannot
observe, such as the destination colour description outside a paint pass, are
reported as unknown. About, the copy action and the transition logging below
remain unimplemented.

Always emit the initialization identity at information level in both Debug
and release builds with the default logging configuration. Use the effect's
logging category for diagnostic state transitions: candidate selection and
rejection reasons, effective configuration changes, processing-path selection,
resource failures and recovery. Keep detailed transition tracing at debug
level, independently selectable through logging configuration; turning on the
developer overlay does not itself enable verbose logs. Warnings identify
actionable failures and the resulting fallback. Suppress repeated identical
events and keep per-frame metrics out of routine logs.

Snapshots and logs must distinguish configured intent from actual state and
include enough context to associate a transition with its game/output and build.
Do not collect or dump complete process environments, credentials or unrelated
application data. Resolution diagnostics report the selected method and outcome;
they do not expose arbitrary command-line arguments or environment values.
The diagnostic path must remain bounded and must not block rendering or query
the GPU synchronously.

### Settings page

Configuration follows KWin's own pattern:
`upscaleconfig.kcfg` and a page registered as `X-KDE-ConfigModule` in System
Settings. The page implements the controls below. A recognized application is
asked for a resolution by its recorded method: at output binding for Wayland,
or after its window appears for X11. Unlisted programs receive no request by
default. The optional “Try other applications too” setting tries native Wayland
mode advertisement; it does not provide generic X11 control. The status can be refreshed
explicitly and reports supplied buffer dimensions, not internal game rendering
resolution. What was requested is reported apart from what the application
committed, and neither a saved preference nor a made request is ever presented
as a successfully applied client resolution.

| Control | Behaviour |
| --- | --- |
| Enable upscaling | Process eligible fullscreen and profiled full-output borderless windows; disabling restores normal KWin rendering. |
| Scaler | Show FSR 1 for the initial implementation. Offer a selector when several scalers are implemented and supported. |
| Mode | Upscale only, until a mode from the processing modes section above is implemented. Offer a selector only for modes that are implemented, and show why an unavailable mode does not apply to the current buffer. |
| Preferred game resolution | Automatic follows a matching profile’s preset, otherwise the supplied buffer; explicit presets or a percentage select a target in physical pixels. |
| Resolution control | Enabled by default for recognized applications; disabling stops requests while eligible supplied buffers can still be scaled. |
| Try other applications too | Off by default; opt into experimental native Wayland advertisement for unlisted executable identities. |
| Minimum output pixels | Scale only when the output’s physical width × height exceeds the threshold; default 2073600, zero disables the threshold. Application entries can inherit or override it. |
| Resolution preset | Native, Ultra Quality, Quality, Balanced, Performance, or Custom; changing the slider selects Custom. |
| Sharpening | RCAS switch, initially off, and a 0–100% strength slider. Zero bypasses sharpening; increasing the value increases strength. The UI must not expose AMD's reversed parameter directly. |
| Status | Desired input, actual supplied input, destination resolution, active scaler, and a reason when upscaling is inactive. Show HDR and VRR information only to the extent actually known. The reason names the one condition that refused the window, not the general eligibility rule. |
| On-screen display | The master switch, the announcement and summary choices with their timeout, and the persistent statistics and developer information choices. Their defaults come from the build type; the page writes an entry only where the user's choice differs from that default. |

HDR and VRR follow KWin's display settings. They are mandatory supported paths,
not optional quality presets. An enabled VRR setting must not be labelled as
proof of currently variable presentation.

### Language and translations

The effect is KDE user interface and follows KDE's translation conventions.
English is the source language; **German, French and Spanish are required**,
and adding another language must be adding a catalogue, never a code change.

- Every user-visible string goes through KI18n with the project's translation
  domain, `kwin_effect_upscale`, which the build defines for the effect and for
  the settings module. That includes the status text, the on-screen display,
  the settings labels and the messages that name a refused condition.
- Do not assemble a sentence from translated fragments into new grammar. Where
  a fragment is unavoidable, as with a refusal reason that appears inside the
  status, the announcement and the developer view, give it `i18nc` context
  naming the frames it appears in, so a translator can see the whole sentence
  and reorder it. A language whose word order differs from English must be able
  to produce a correct sentence without changing the code.
- Values follow the user's locale for dates, times and decimal separators.
  Pixel counts are the deliberate exception and are never grouped: a resolution
  is an identifier, not a quantity, and "3.840 × 2.160" reads as two fractional
  numbers.
- The plugin metadata carries translated `Name` and `Description` entries,
  because the effects list reads the metadata and never calls into the plugin.
- Extraction and catalogues follow KDE's layout: a `Messages.sh` at the
  repository root produces the template, catalogues live in
  `po/<language>/kwin_effect_upscale.po`, and `ki18n_install(po)` installs the
  compiled catalogues. The packages ship them, so a user who installs the
  package gets their language without any further step.
- Acceptance runs the settings page and the on-screen display in each shipped
  language and confirms that no user-visible string is left untranslated, that
  a longer translation does not break the settings layout or push the display
  off the output, and that the display still follows the scaling rules above.

### Per-application overrides

Implemented: the settings page can create profiles manually or from KWin’s
interactive window selection, inspect/edit them, disable shipped entries,
delete user entries, and restore the shipped catalogue. It edits the name,
window class/instance, executable basename, method, preset and pixel threshold.
Saved changes are sparse relative to shipped fields; untouched fields follow
package updates. Native explicitly opts out of scaling. Disabling an entry
removes its participation in matching; it is not a Native opt-out rule.

Still required: general sparse per-setting overrides (including sharpening and
OSD choices), explicit inheritance controls for those settings and ordering in
the editor. A stored Order field already determines matching precedence. False
and zero must remain valid overrides; equality with today’s global value must
not erase an explicit override.

Identify windows through KWin's application ID/window class and optional instance,
using its interactive window detection service when adding a running application.
Do not use process scanning or a changing window title as the primary identity.
Known-application recommendations must remain editable and removable, and must
not overwrite user changes on update. Profiles do not bypass rendering
eligibility or automatically implement client-resolution negotiation.

Two identities are needed rather than one, and they are not interchangeable.
The window class and instance identify a window, which is what the scaler and
the reports work with. The program behind the connection identifies a client
before it has a window, which is the only identity available to the
[advertised screen mode](#telling-one-application-that-its-screen-is-smaller)
and is taken from KWin's own resolved executable path, never from inspecting
processes. A profile that wants that method needs the program as well.

Recommendations that ship with the effect are active on installation rather
than offered as templates, decided by Jens on 2026-09-18: the effect is meant
to work for a known game as soon as the package is installed. The user's
explicit settings still win over a recommendation, and the editable profile
layer must be able to change or remove one.

Shipped entries and user changes are already stored this way, in
`kwinupscalerc` with the defaults installed beside the session's other
configuration defaults. General setting overrides and editor ordering remain
open. The proposed model, code reuse findings,
catalogue policy and required checks are in the
[application profiles slice](agents/slice-application-profiles.md).

### Game detection OSD

The on-screen display (OSD) optionally announces when a game is
recognized by an application profile. Provide a **Show game detection** switch
and a configurable display timeout in seconds. Both settings are currently global; sparse per-application overrides remain
required.

Enable detection announcements by default in all build types. Also provide
**Show basic settings**, initially enabled, to include a short summary in the
same timed message: actual input/output dimensions or scale and the active
shader path, including sharpening when active. If processing is bypassed or
unsupported, state that instead of naming the configured shader as active.
The summary can be disabled independently. Use a bounded, positive timeout,
initially three seconds; the summary shares it and does not extend it on every
frame. After a user changes an effective setting, one brief updated summary may
appear with the same timeout, without claiming a new game detection.

Show the detected game and selected profile on the game's output. Detection
alone must not be presented as proof that upscaling is active or that a desired
resolution was applied. Announce the first match when the game's window becomes
active and visible, including identities discovered after window creation.
Do not repeat the message for every frame, title change or focus return to the
same window and profile. A newly launched game or a different selected profile
can produce a new announcement. Hide the OSD when its timeout expires or the
game ceases to be active and visible; suppress it while the screen is locked.

Use this OSD rather than introducing a second notification surface. Draw
it independently of the game's captured buffer so it remains sharp and cannot
be processed by the upscaler.

**The OSD follows the session's scaling settings.** It is KDE user interface
and must look like it: take the font family and size from the session's font
settings and the scale factor from the output the message is shown on, so the
text is the same physical size as the rest of the desktop on that screen. A
per-output scale applies per output; a changed scale or font re-lays out the
text at the new size rather than stretching what was already drawn. None of
this passes through the upscaler: the text is measured and rendered at
destination pixels, so a game enlarged from a smaller buffer never makes the
overlay blurry or larger. A television at 4K with an unscaled desktop is the
case to keep legible; do not compensate with a size of the effect's own
choosing where the session already states one.

Implemented so far: the surface exists and is drawn after the screen pass, at
destination resolution, outside the captured image, taking no focus and no
input. It scales its text with the output's scale factor; taking the family
and size from the session's font settings, and re-laying out when either
changes, is specified above and not yet implemented. While it is visible the effect reports itself active, because KWin skips
the paint methods of an inactive effect, and a refused window is exactly when
the explanation is needed; the composition requirement that comes with it ends
when the display is hidden. It announces the application and shows the basic summary for the configured
timeout. An application whose identity matches the shipped catalogue is
announced as *recognized* by name; every other window is announced as merely
*selected*, and neither wording claims that a resolution request succeeded.
Matches come from the layered shipped catalogue and user-edited profiles. The announcement is keyed to the selected window:
a repaint or title change does not restart its timeout. Selecting a different
window or explicitly reconfiguring the display starts a new announcement.

### OSD defaults by build type

Expose an OSD enable switch and separate choices for timed detection, basic
settings, persistent statistics and **Developer information**. The developer
option adds the complete active configuration and runtime state to the
persistent statistics view. An overall Off hides every OSD mode without
changing effect settings or disabling logging.

Implemented: the switches exist in the settings page and in `kwinrc` as `Osd`,
`OsdDetection`, `OsdSummary`, `OsdStatistics`, `OsdDeveloper` and `OsdTimeout`.
The two persistent choices default to the build configuration of the binary
that reads them, decided by whether `NDEBUG` is defined, which is exactly the
Debug versus Release distinction above. A choice equal to that default is
stored as no entry at all, so a build type's default is never written back as
if the user had chosen it.

| Setting when no explicit preference exists | Debug build | Release build |
| --- | --- | --- |
| OSD enabled | On | On |
| Timed game detection | On | On |
| Timed basic scale/shader summary | On | On |
| Persistent statistics | On | Off |
| Developer information | On | Off |

Use the actual build configuration: only `Debug` selects developer defaults;
`Release`, `RelWithDebInfo` and `MinSizeRel` select release defaults. For a
multi-configuration build, use the configuration of the built binary. Debug
symbols alone do not enable developer defaults. Apply defaults only to absent
preferences. Preserve explicit user choices, including Off, through upgrades
and switching build types; do not write inferred defaults as user overrides.
Future application profiles follow the same explicit-override rules.

In a Debug build the persistent view appears for the selected active window
without an extra opt-in. In a release build only the brief timed announcements
appear by default. Users may enable statistics and developer information in
release builds and disable them in Debug builds. Detection timeout never hides
a deliberately enabled persistent view. With no active window, or while locked,
do not retain an overlay showing a previous game's state.

### In-game controls and applying settings

Required extension, not yet implemented: extend the same overlay with an
on-demand settings panel and configurable shortcuts. Let users enable or
disable scaling, adjust sharpening, select available filters and geometry, and
change the desired resolution without leaving the game. Show the selected
game/profile and whether a value is inherited or explicitly overridden. An
explicit **Apply to this game** saves profile overrides; **Use global** clears
an override. Keep editing global defaults a separate, labelled action.

The panel must distinguish configured, effective and pending values. Classify
each change by the active control method's verified capabilities:

| Change | When it takes effect |
| --- | --- |
| Effect enable, sharpening, supported filter/geometry, overlay visibility | Apply during play, without restarting the game. Geometry also requires working input mapping; unavailable combinations remain disabled with a reason. |
| Preferred resolution with a verified live negotiation path | Request during play and show the pending target until an actual buffer change confirms the result. Report ignored or adjusted requests accurately. |
| Preferred resolution controlled in the game's own settings | Show the requested pixels as guidance; applying our settings does not establish that the game changed resolution. |
| Wayland advertisement changed after the client read its outputs | Save for the next normal launch and retain the observed state of the running game. |

Mixed changes apply their live portion immediately and keep only the remaining
portion pending. Reverting a pending value to the effective value clears that
pending change. Saving or applying settings must never restart a game on its
own. Explain when a new normal launch is needed; managed relaunch is outside
the [agreed scope](#restarting-a-game-with-pending-settings).
The controls must remain available when a matched game is not being upscaled.

Provide a temporary visual comparison between ordinary KWin scaling, FSR and
FSR with sharpening at unchanged supplied-buffer and destination dimensions.
Comparison must not alter saved settings or negotiate another resolution.
For an optional split view, both sides must use the same source frame. Keep
comparison separate from performance measurement because showing two paths
adds work.

### Optional statistics overlay

Add an optional persistent statistics
view to the same OSD, independently switchable from the brief game-detection
announcement and the interactive settings panel. Use the build defaults above,
provide a shortcut to show or hide it, and allow global defaults with sparse
profile overrides for visibility and displayed fields. Detection timeout must not hide
a statistics view the user has enabled.

Useful fields are the game/profile, actual supplied-buffer and destination
dimensions, desired resolution when different, active filter and sharpening,
effective resolution method, pending restart, frame rate and frame time. Show
the drawn image dimensions as well as the output size when black bars are used.
Give a specific reason when scaling is inactive. Colour/HDR information and
presentation state may be included only to the extent actually observed.

Label every timing measure by what is counted: client buffer updates,
presentation events or output refresh rate. A compositor repaint counter or
configured refresh rate must not be presented as the game's rendered FPS.
Deduplicate the same update drawn in multiple passes, state the sampling
interval, and show unavailable or stale values honestly, including when a
game stops supplying frames. GPU filter timing is an optional field only where
supported and measured; it is not total game GPU time or end-to-end latency.
Configured HDR/VRR settings alone do not prove the corresponding active path.

Implemented: the persistent view shows what the effect is doing with the
buffer, the supplied and destination sizes, the output, and separately counted
client buffer updates and compositor repaints with their one-second sampling
interval and the age of the sample. A game that stops supplying frames
therefore shows an ageing sample rather than a frozen rate presented as
current. Status and developer details also report desired resolution, matched
profile, control method, advertisements and X11 requests/failures. Presented
output frames are counted separately, with average rate, 1% low, 99th-percentile
frame time, worst frame time, sample count and presentation mode. Black bars,
managed restart and GPU filter timing are not implemented.

Use event-driven samples and bounded text updates, with no synchronous GPU
readback or continuous full-screen repaint loop just to animate statistics.
Hiding the view stops its own sampling overhead and releases its resources and
any composition requirement. Watching the screen's presented frames outlives
it, because reports are asked for while the view is off; that costs one signal
per presented frame and no drawing. Draw text at output resolution after the game
pass, outside the captured image. Keep passive statistics from taking focus
or input; restore game focus and pointer state after closing interactive
controls. Suppress all overlay modes while locked and discard stale game data
when the selected game closes or changes outputs.

#### Developer information

Add **Developer information** to the OSD settings. It extends the ordinary
FPS/resolution view with the complete effective configuration and diagnostic
state, grouped and labelled so developers can explain what the effect is doing.
Keep the passive view readable at the session's scale, by the same rule as the
timed messages above; detailed inspection and copying are also available
through the settings diagnostic snapshot. This is
required development infrastructure, not the optional full About overlay.

Implemented: build and runtime, selection, configuration, geometry, processing
and colour are populated from the same snapshot, alongside the measurements
above. The matched application and method are reported, as is KWin’s observed
presentation mode when frames have been presented. This does not establish
physical-panel VRR acceptance. General override origin, drawn-image bars,
intermediate formats and GPU filter timing remain absent.

| Group | Required information when available |
| --- | --- |
| Build and runtime | Loaded plugin version, branch/tag, full revision and build time from the shared identity record; build configuration, KWin/Qt versions and active graphics backend. Preserve access to full values if the passive view abbreviates them. |
| Selection | Selected application/window identity and output, selection or rejection reason, profile and match origin when implemented, active/visible/fullscreen state, and ambiguous or missing candidates. Do not call an arbitrary fullscreen client a recognized game. |
| Configuration | Every implemented setting's effective value, with global/profile origin where supported; include effect enable, scaler, sharpening enable/strength, desired input, geometry and OSD choices. Distinguish requested and pending values from those currently applied. |
| Geometry | Actual supplied buffer, desired buffer, destination and drawn image dimensions, scale factors, output scale/transform, viewport and bars where relevant. |
| Processing | Active shader/pass path, enabled versus bypassed stages and exact fallback or ineligibility reason, resource readiness/failure, capture/intermediate formats and the effect's own scanout-blocking state. |
| Colour and presentation | Known input/output colour descriptions, transfer functions, HDR state, configured versus observed VRR/presentation state, and limitations of the available observations. |
| Measurements | The ordinary FPS/frame-time fields, their event source and sampling interval, sample freshness, and optional measured filter timing; never substitute output refresh for game FPS. |
| Resolution and launch | Current control capability/method, requested versus confirmed input, pending restart and last operation outcome when those features exist. |

Update this inventory as implemented settings and states grow. A consistent
snapshot must not combine a new window's settings with the previous window's
buffers. Distinguish unknown, stale, unsupported and not-yet-implemented fields;
do not implement out-of-scope launching or future rendering modes just to fill
them. Report only state actually exposed by KWin/the effect, and identify the
scope of observations such as the effect's own scanout block rather than
claiming knowledge of the whole compositor. Apply the same bounded sampling,
capture exclusion, focus, lock-screen and cleanup rules as ordinary statistics.

### Percentage and pixel resolution

The slider expresses the desired input size as a percentage of the covered
output's physical pixel width and height, independently of Plasma's desktop
scale. It ranges from 50% to 100% for the initial FSR path, and only the
proposed supersampling mode would extend it above 100%, with one-percentage-
point steps for custom values and keyboard operation. Both dimensions use the
same factor; there are no independent width and height sliders. Show the
rounded integer pixel dimensions beside the percentage before applying it.
At 50%, each dimension is halved: the pixel count is one quarter, not one half.

Presets retain the exact scale ratio rather than deriving dimensions from the
rounded percentage label. The familiar FSR names and ratios follow
[AMD's FSR 1 quality modes](https://gpuopen.com/fidelityfx-superresolution/#amd-fidelityfx-super-resolution-quality-modes).
They select a desired input size, not different EASU shader implementations:

| Preset | Input size per dimension | Example input for 3840 × 2160 output |
| --- | --- | --- |
| Native | 100% | 3840 × 2160 |
| Ultra Quality | 1 / 1.3, approximately 76.9% | 2954 × 1662 |
| Quality | 1 / 1.5, approximately 66.7% | 2560 × 1440 |
| Balanced | 1 / 1.7, approximately 58.8% | 2259 × 1271 |
| Performance | 1 / 2, 50% | 1920 × 1080 |
| Custom | User-selected percentage; for example 75% | 2880 × 1620 at 75% |

These examples round each dimension to the nearest integer, with halves
rounded up. Allow the resulting subpixel aspect-ratio rounding discrepancy
when checking geometry; larger mismatches remain unsupported. A client's
supported modes or scale granularity may differ. Display any negotiated size
as such, separately from the calculated wish and the actual committed buffer.
The example dimensions are calculated targets, not guaranteed game modes.

Automatic is the default and makes no resolution request of its own; a
recognized application then follows the size recorded for it in the
[catalogue](#the-recognized-applications-shipped-with-this-effect), so that
installing the effect is enough for a known game on an eligible output.
Selecting Native bypasses both resolution requests and processing, even if the
application supplies a smaller buffer. An application's Native rule also wins
over an explicit global scaling preset. At actual native resolution the initial
effect bypasses both EASU and RCAS; the proposed sharpen-only mode would be
the explicit exception, and it does not change this default. Turning the
effect off is a separate action.
On an output change, recompute the desired pixels from the stored percentage
or preset; never change the monitor mode or desktop scale to satisfy the wish.

**Minimum output pixels.** The global `MinimumPixels` setting defaults to
2,073,600 (1920 × 1080). Each application rule can override it; `-1` in a rule
inherits the global setting and zero disables the threshold. Compare the
output's current physical width multiplied by its physical height, before
applying the preset or percentage. At or below the threshold the effect makes
no reduced-resolution request and bypasses FSR. This is an output eligibility
threshold, not a lower bound on the requested buffer size. Full HD and
1080 × 1920 therefore bypass at the default; 2560 × 1080 and 3840 × 2160 exceed
it. Desktop scale and logical window dimensions do not change the comparison.
The pixel count approximates resolution-related rendering cost; it does not
measure refresh rate or application complexity.

Selection and policy are independent for each output, including secondary
outputs. One eligible fullscreen or profiled full-output borderless surface
can be scaled on each output simultaneously. Multiple eligible surfaces on
the same output remain refused. A Native rule or a threshold bypass on one
display cannot veto another display's candidate. Threshold bypass leaves normal
KWin rendering in place if a client retains an independently chosen smaller
buffer; it cannot force an application's internal rendering to native size.

### What a resolution wish can control

The preferred resolution is a best-effort target. Whenever a supported control
path is available, attempt to reach it. The actual committed buffer always
determines scaler input and eligibility. A game with a fixed resolution is
scaled from that resolution if it is otherwise eligible, even when it differs
from the wish. A mismatch alone must not disable upscaling. Geometry outside
the supported range still uses KWin's normal rendering.

The effect receives a finished image. A game's internal 3D render resolution
can differ from the submitted buffer size, for example when an in-game
upscaler already produces a native-size image. Label the observed value as
**supplied input resolution**, not as a measurement of internal rendering.
Shrinking a completed native-size image and enlarging it again cannot save
the game's rendering work and must not implement this slider.

| Route | What is possible | Project decision |
| --- | --- | --- |
| Game's own settings | The game selects a smaller output buffer or its own internal render scale. An internal scale alone need not produce a smaller submitted buffer. | Always offer the calculated desired pixel size as guidance; verify what buffer actually arrives. |
| Cooperative native Wayland client | A compositor can suggest a preferred surface scale. The client must support and act on that hint. | Investigate for this slice; enable requests only after the supported KWin integration and client behaviour are verified. |
| Proton/Xwayland game | Resolution selection and delivery depend on the game and Xwayland integration. The Wayland hint is not a generic control for Windows game render settings. | Verify separately on a real game; otherwise report automatic control as unsupported for that path. |
| Per-game display information | Advertising a smaller fullscreen resolution can cause the game to select a smaller buffer. | **Implemented for cooperating native Wayland clients with an enabled `AdvertisedMode` or `AdvertisedModeAndScale` profile:** the effect tells that connection alone about a different current mode when it binds the output. Verified with SuperTuxKart's OpenGL path; clients that ignore mode information are unaffected. Xwayland games require the separate X11 method. |
| Virtual output | KWin can create an additional output, visible to clients of the shared session. | Not an isolated per-application resolution control; not implemented. |
| Nested compositor | A separate environment can advertise chosen screen modes, as gamescope does. | Out of scope: it requires wrapping the game launch. Retained only as research. |
| Cooperative X11 client | Resize its window so its own X connection can select an emulated mode. | Implemented as X11Resize with bounded validation and restoration; no shared output change. |

The [Wayland fractional-scale protocol](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/fractional-scale/fractional-scale-v1.xml)
defines a preferred scale relative to surface-local dimensions, in units of
1/120. It is a suggestion, not an acknowledgement of a changed buffer or an
API for the game's internal render resolution.
[KWin 6.3.6's SurfaceInterface](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/wayland/surface.h)
exposes `setPreferredBufferScale()`. That is an integration lead, not evidence
that an effect can safely override KWin's scale policy. Any implementation must
account for logical versus physical sizes, protocol quantisation, output
changes and restoration of KWin's normal scale preference.
KWin 6.3.6's `Window::setNextTargetScale()` is another integration lead: unlike a
direct surface hint, it also changes the scale used for subsequent fullscreen
configures. It participates in KWin's geometry and output policy, so it requires
coordinated ownership and restoration. Neither the preferred scale nor KWin's
target-scale state proves that a client changed its buffer; status must observe
the committed buffer dimensions.
[Gamescope's README](https://github.com/ValveSoftware/gamescope#gamescope-the-micro-compositor-formerly-known-as-steamcompmgr)
describes its separate virtual-screen approach.

The desired percentage remains visible as a clearly labelled target when no
verified request mechanism is available. Report the unsupported automatic path;
asking the user to change game settings does not satisfy resolution control.
If a supported request is sent, show it
as pending until a committed buffer confirms the result. Never label a saved
preference as applied merely because the configuration was accepted. If the
client ignores or adjusts it, show the actual dimensions and continue scaling
eligible buffers at their actual size. Do not repeatedly resend an ignored
request or repaint continuously while waiting. Apply a request on slider
release or explicit Apply, rather than at every drag position.

### Known compatibility limits

Resolution control is cooperative, not a universal buffer-size override. The
following boundaries apply to the implemented methods. A smaller X drawable,
an advertised mode and a smaller submitted buffer are three different
observations; none alone establishes reduced internal rendering cost.

| Gap | Open-source example and evidence | Current consequence |
| --- | --- | --- |
| X11 renderer ignores resizing or does not request mode emulation | glmark2 2023.01, X11: a targeted research run reduced its drawable to 1080p but retained a 4K rendering viewport and lost full-output coverage. Its event loop does not handle resize events. | `X11Resize` cannot make this client cooperate; bounded negotiation restores geometry and refuses an unsupported request. |
| Fullscreen-desktop Wayland client ignores advertised mode | SuperTuxKart 1.4, Vulkan: advertising 1080p still produced a 4K buffer. Its Vulkan driver obtains swapchain dimensions from `SDL_Vulkan_GetDrawableSize`. Its OpenGL path responded in separate tests. | Mode advertising is renderer-dependent; Vulkan support cannot be inferred from the OpenGL result. |
| Integer scale cannot express the target | glmark2 2023.01 Wayland and vkmark 2025.01 read scale differently from mode-only clients. The implemented scale methods cannot reduce a scale-1 desktop through a smaller positive integer scale. | No reduction at scale 1; other desktop scales allow only discrete reachable sizes. Report the reachable request separately from the configured wish. |
| Toolkit selects the wrong output | Extreme Tux Racer 0.8.4 with SFML 2.6.2 moved from the secondary display to the primary when recreating its fullscreen window. SFML explicitly selects the primary RandR output. | The shipped profile refuses resolution control on secondary outputs before resizing. Other clients can scale there; secondary displays are not generally excluded. |
| Requested X11 mode is absent | SFML validates fullscreen modes against its available-mode list; the regression fixture rejects a 2259 × 1271 request on the tested 4K output. | Arbitrary percentages are not guaranteed for X11. The controller refuses missing modes instead of changing the shared output or silently claiming the requested size. |
| Smaller window loses full-output presentation | The negative glmark2 X11 case supplies no client-owned emulation. Normal smaller borderless windows are not full-output surfaces. | Borderless eligibility requires a profiled, undecorated window covering exactly one output. Shrinking an ordinary window is not a substitute for preserving its destination and input mapping. Native Wayland borderless coverage needs separate real-application acceptance. |
| Internal render targets remain fixed | SuperTux 0.6.3, SDL/X11 borderless: the traced outer buffer and viewport changed from 4K to 1080p, while an intermediate framebuffer stayed 1368 × 769. | This proves control of the supplied buffer, not proportional GPU savings or control of every internal target. |
| Translation and physical-session coverage is incomplete | Wine/Proton paths, mixed output scales, hotplug, pointer confinement and physical HDR/VRR have not completed the production acceptance matrix. | These are unverified combinations, not demonstrated failures of every application using them. |

Source entry points for these findings are
[glmark2's X11 backend](https://github.com/glmark2/glmark2/blob/2023.01/src/native-state-x11.cpp),
[SuperTuxKart's Vulkan driver](https://github.com/supertuxkart/stk-code/blob/1.4/lib/graphics_engine/src/ge_vulkan_driver.cpp),
[SFML's X11 window implementation](https://github.com/SFML/SFML/blob/2.6.2/src/SFML/Window/Unix/WindowImplX11.cpp)
and [SuperTux's OpenGL video system](https://github.com/SuperTux/supertux/blob/v0.6.3/src/video/gl/gl_video_system.cpp).
These version-specific findings do not establish the behaviour of newer releases.
The [resolution-control work package](agents/slice-resolution-control.md#source-led-compatibility-investigations)
owns the reproductions and source-led solution investigations. A solution must
still run from the installed plugin, leave unrelated clients and output modes
alone, require no external patches or game reconfiguration, and preserve
presentation and input. A precise unsupported result is preferable to claiming
that a smaller drawable fixes an uncooperative renderer.

### Selecting the resolution control method

Implemented per profile: None, advertised mode, advertised scale, advertised
mode and scale, and X11 resize. There is no global method selector or automatic
method discovery. The following global-selection design remains unimplemented:
provide **Resolution control method**
in global settings, defaulting to **Auto**, with a sparse per-application
profile override. **Use global** inherits the current global method; an
explicit **Auto** override keeps automatic selection for that application even
if the global method later changes. Game detection selects the profile; it
does not by itself establish that resolution control succeeded.

| Method | Intended behaviour |
| --- | --- |
| Auto | Choose a verified compatible method for the selected application and runtime. In-session negotiation only: the [four requirements](#four-requirements-that-bound-every-route) leave no launch-time method to fall back to. |
| Advertised screen mode | **Implemented for verified native Wayland client/runtime combinations.** Tell one recognized native Wayland client that its screen has a smaller current mode when it binds the output. This does not control Xwayland games. It needs no launch helper or restart and changes nothing outside that connection. |
| Wayland negotiation | Generic surface-scale negotiation remains experimental; the implemented advertised scale and mode-and-scale methods are separate profile choices. |
| X11 window resize | **Implemented.** Request a smaller drawable and require client-owned fullscreen emulation to retain output coverage. |
| Display proxy | **Out of scope.** Sommelier's direct-scale mode supplied smaller buffers, native and through a private Xwayland, but every form of it starts the game. Kept as a measured mechanism and as technique worth reading, not an offered method. |
| Gamescope | **Out of scope.** It forwarded smaller original buffers in testing, but the game has to be started through it, and its image arrives as a child surface this effect rejects. Kept here as a measured mechanism, not an offered method. |
| Game settings only | Make no automatic resolution changes; show the desired pixels as guidance and scale eligible supplied buffers. |

Only implemented and verified methods may be enabled for the current case.
Show unavailable methods with a reason. An explicit method must not silently
switch to another method when it fails. Auto may use a verified alternative,
but must report the effective method and whether the target was reached.
Method selection is independent of the desired resolution, EASU and RCAS.
The existing Automatic resolution preset follows the matched profile’s preset;
with no profile target it makes no request. The implemented None method makes
no request but permits scaling of eligible supplied buffers. Native disables
scaling independently of method selection.

A running Wayland client may have cached its output information; changing the
profile does not make that client re-enumerate. Report observed dimensions and
any advertisement separately. X11 requests can act live on cooperating clients.
Neither path restarts the application or cycles through launch helpers.

### Fitting a request to what the machine can actually do

A setting is a wish. Between the wish and a scaled frame sit the GPU, the
driver, the shaders, the output's colour handling and the game itself, and each
can refuse. The rule this effect follows is that every one of those limits is
**asked for at runtime and never assumed**, because the machine it was written
on is not the machine it will run on: KDE runs on drivers whose largest texture
is a quarter of this one's, on OpenGL ES where high shader precision is
optional, and on screens whose colour handling differs from a desktop monitor's.

| What can refuse | How the effect finds out | What happens when it refuses |
| --- | --- | --- |
| OpenGL version | `hasVersion()` on the context KWin handed over | the effect reports itself unsupported and KWin never loads it |
| High precision in fragment shaders | `glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT)`, on OpenGL ES where the language makes it optional | unsupported, for the same reason: medium precision cannot address a 4K pixel grid and loses detail while sampling |
| Shader compilation and linking | KWin's own shader manager, at initialization | the effect falls back to ordinary rendering until it is reconfigured |
| A floating-point render target | allocate one, replace the storage where the ES allocator ignores the format, then check `glGetError` and framebuffer completeness | the same fallback; nothing is filtered through an 8-bit image that merely looked complete |
| Largest texture | `GL_MAX_TEXTURE_SIZE`, read once per reconfiguration where a context is current | the allocation is refused; the value is in the developer information, so a report from unknown hardware carries it |
| The destination's colour handling | the `ColorDescription` of the frame being painted: its transfer function must be one the shaders decode, and its luminances must be finite and ordered | the window is refused, naming colour handling |
| The buffer the game supplied | its DRM format code, read from the surface | refused, naming the format code, so the unknown one can be looked up |
| The orientation of the frame | `RenderTarget::transform()` of the frame being painted | flips are handled by the projection matrix and drawn through; anything else is refused by name and the value is reported |
| The scaling ratio itself | `upscaleSizing()` against the committed buffer | refused as not smaller, below half, or a different aspect ratio: three distinct answers, because they need three different fixes |
| What the game did with the request | the committed buffer size, observed | reported beside the advertised size, never in place of it |

Colour refusals are scoped to individual windows. An output colour or
configuration change, or moving the window to another output, permits a new
attempt without reapplying effect settings. Graphics allocation failures retain
their separate reconfiguration requirement; neither case uses a repaint loop
to poll for recovery.

The resolution wish is calculated from the preset or percentage against the
output's real pixel size, so it follows whatever screen is attached. It is
constrained by one rule of the algorithm rather than by a table of modes: FSR 1
enlarges by at most a factor of two per dimension, so a request below half the
destination would produce a buffer the scaler then refuses. The presets are
defined inside that range, and the checks cover every whole percentage against
several real display sizes, including ultrawide and portrait ones.

Sizes are not rounded to standard modes or to even numbers. That was measured
rather than assumed: a client was told 2259 × 1271, the awkward size that
1 / 1.7 of 3840 × 2160 produces, and committed exactly that, and the shader
tests cover odd widths, odd heights and destinations that are not whole
multiples of the source. Inventing a snapping rule would discard resolution the
user asked for in exchange for a constraint that this Wayland path does not
impose. The X11 path separately requires an available emulated mode; an absent
odd-sized mode is refused rather than rounded or advertised as achieved.

What remains genuinely unknowable in advance is the application. No query
establishes whether a program will act on the mode it is told, so the effect
states what it advertised, observes what arrived, and reports the two
separately rather than presenting the request as a result.

### Telling one application that its screen is smaller

This is the effect's implemented mode-advertising path for a game the user
starts themselves. It is deliberately narrow, and what it cannot do is as
important as what it can.

**When it acts.** A program decides how large an image to render from the
display information it was given when it connected, long before it has a
window. Measured on KWin 6.3.6 against native Wayland SuperTuxKart: a fractional scale
hint, a rewritten output mode and a smaller window all fail to reduce what it
renders, and the smaller window is actively harmful because the game keeps
rendering at full size and scales its own finished image down. The effect
therefore advertises the mode when the client binds the output, and does
nothing through this method to a game that was already running when the effect
was loaded. Other clients can follow live resize requests; those are a
separate mechanism.

**What it changes.** The current and preferred mode sent to that one client's
output resources. The output keeps its mode, the desktop keeps its scale,
every other application keeps the display information KWin gave it, and the
user's own game settings are never written. Nothing has to be restarted:
KWin loads the effect at the start of the session, and the user starts the
game after that.

**How the size is chosen.** From the preset or percentage, against the pixel
size of each output, exactly as the desired resolution is calculated
everywhere else. Arbitrary calculated sizes are honoured, so a preset is not
restricted to standard modes. When the global preset is Automatic, which means
the user has not chosen, a recognized application uses the size recorded for
it in the catalogue; an explicit global choice wins except for an application's
Native opt-out. The output pixel threshold is checked before either request.
Advertising
the size the output already has is not a request and is not sent.

**Which application.** Only one whose program matches an enabled entry in the
layered shipped/user catalogue, unless the user explicitly enables attempts for
unlisted applications. That fallback uses advertised mode and is off by default.
At the moment of the bind no window exists, so there is no window class to
match: the identity available is the executable path KWin resolved for the
connection, and only its file name is compared, because the same game lives in
different directories depending on how it was installed. The window class and
instance identify the window later, for reporting and for the scaler.

**What it is not.** It is not enforcement. A program that ignores mode
information, or that asks the compositor for its fullscreen size instead of
selecting a mode, keeps its own resolution; SuperTuxKart's Vulkan renderer is
a measured example of the latter. The advertised size, the desired size and
the committed buffer are therefore three separate values, and status reports
them separately. This Wayland output method cannot address one Xwayland game:
Xwayland binds the output while KWin starts, before any effect is loaded, and
serves every X11 application from one connection.

**Its visible cost.** The game's own settings screen will offer resolutions
only up to the advertised size, because that is what the game believes the
screen is. Nothing outside the game observes a difference.

#### Telling one application that its screen has a smaller scale

Some clients never look at a display mode. They render the logical screen size
multiplied by the scale they were told, and declare that scale on their own
surface, which is how a program draws sharply on a high-density screen. For
those, the lever is the scale rather than the mode, and it works because the
compositor divides the buffer by the scale the client declared: a client told a
smaller scale renders fewer pixels and still covers the whole screen.

A third kind takes its fullscreen size from the mode in pixels but declares the
output's scale on its surface. Giving it either alone leaves the two
disagreeing, and the image stops covering the screen: the mode alone shrinks
the window away from the edges, the scale alone stretches it past them. Such a
client is told both, and the two are chosen to agree.

Which kind an application is was read in its source and then confirmed by
running it. It cannot be guessed from what it does, because all three look the
same from outside until the request is made.

**This lever is coarse, and that is a property of Wayland, not a shortcut.**
The output scale in the protocol is an integer, so the only sizes reachable
are the logical screen multiplied by a whole number. A screen at scale 2 offers
exactly one reduction, a half. A screen at scale 3 offers two thirds and a
third, and the third is below what FSR 1 enlarges from, so only two thirds is
usable. **A screen at scale 1 offers nothing at all**, and an application of
this kind is then reported as having no reduction available rather than being
sent a request it would ignore.

The wish is therefore answered with the reachable size nearest to it instead of
being refused for not being reachable exactly. Asking for a quality reduction
on a screen that can only halve gets the half, and the status reports the size
that was actually asked for beside the one that was calculated. Steps the
scaler would then refuse are never offered.

#### The recognized applications shipped with this effect

Installing the package is meant to be enough for a game the effect knows, so
these entries are active without the user configuring anything.

They live in `kwinupscalerc`. The effect installs its own copy of that file
beside the session's other configuration defaults, replaces it with every
package, and never writes to it. A user's own applications and changes go to
their file of the same name in their configuration directory, and KConfig
layers the two: a field nobody changed keeps following the installed package,
so a later version can correct a method or add a game without disturbing an
edit, and a field the user changed always wins. Nothing is ever copied from one
file into the other, because a copy stops receiving corrections the moment it
is made.

Both layers name an entry the same way, `[Application-<identifier>]`, which is
what lets them describe one application between them. An identifier generated
per installation could not do that.

Restoring the list therefore means discarding the user's file rather than
copying anything: fields they overrode go back to what the installed package
says, and applications they added are removed. It is deliberately separate from
restoring the settings on the same page, because the two are different kinds of
data — the settings are values this effect defines, the list is data it ships
and the user extends — and one button doing both would surprise people. The
settings page states whether the list differs from the shipped one and offers
restoration when it does; the editor displays the entries themselves.

Every field was read off a running instance of the stated package version. A
name never implies an identity, and a version is recorded with each entry so
that a later mismatch can be traced rather than guessed at.

| Application | Measured version | Window class | Instance | Program | Method | Resolution when the global preset is Automatic |
| --- | --- | --- | --- | --- | --- | --- |
| SuperTuxKart | 1.4 | `supertuxkart` | `supertuxkart` | `supertuxkart` | Advertised screen mode | Quality, 1 / 1.5 |
| Extreme Tux Racer | 0.8.4 | not constrained | `etr` | `etr` | X11 window resize, primary output | Quality |
| glmark2 | 2023.01 | `com.github.glmark2.glmark2` | `glmark2-wayland` | `glmark2-wayland` | Advertised screen scale | Automatic, no request |
| vkmark | 2025.01 | `com.github.vkmark.vkmark` | `vkmark` | `vkmark` | Advertised screen mode and scale | Automatic, no request |

Two details in that table are the reason identities are measured.

Extreme Tux Racer reports its window class as `Extreme Tux Racer 0.8.4`, with
the version in it, so an entry matching the class would stop matching at the
next package update. Its instance name is the stable field, and the entry
constrains that alone.

Extreme Tux Racer uses [X11 window resizing](#per-window-x11-resize-and-fullscreen-emulation).
Its profile requests Quality by default, giving 2560 × 1440 on a 3840 × 2160
output without editing game settings. SFML 2.6.2 always selects the primary
RandR output when recreating a fullscreen window. The profile therefore sets
`X11PrimaryOutputOnly=true`: requests on another output are refused before
resizing, so the game does not unexpectedly jump between displays. This is a
client limitation, not an Xwayland server per display.

SuperTuxKart carries a preset of its own so that a fresh installation already
does something. The user's explicit choice always wins over it; Automatic means
the user has not chosen, not that nothing may happen.

The two benchmarks deliberately carry no preset. A benchmark exists to measure
a machine, and quietly halving what it renders would make it report a number
for something nobody asked for. They follow an explicit setting like anything
else, which is what makes them usable for measuring this effect: the same
binary, the same scene, once at the screen's resolution and once reduced.

Their methods differ from SuperTuxKart's because their sources read different
things. glmark2's Wayland backend takes the size the compositor configures,
multiplies it by the advertised scale and declares that scale on its surface,
consulting the advertised mode only when a fullscreen request was refused.
vkmark takes its fullscreen size from the advertised mode in pixels and
separately declares the advertised scale, so its image covers the screen only
when both are given together. Neither was guessed from behaviour alone; both
were read in the source and then confirmed by running them.

### Application launch configuration and method discovery

Managed launch configuration and launch-time method discovery are outside the
[four requirements](#four-requirements-that-bound-every-route). Earlier proposals
for executable arguments, environments, runtime wrappers and trial relaunches
were superseded by the requirement to start games normally. No launch helper,
launcher adapter or discovery controller is implemented or required.

The profile’s Program field is an executable basename used to recognize a
Wayland connection before its windows exist; it is not a command to run.
Compatibility measurements must still record actual buffers, presentation,
input and lifecycle, and cannot label a request as successful without observation.

### Restarting a game with pending settings

The effect does not close or relaunch games. Resolution changes that only affect
initial Wayland display enumeration apply when the user next starts the game
normally. Live X11 requests use the existing window and validate its response.
Changing a setting must never be represented as proof that an application
changed its buffer.

### Optional later extensions

The following are accepted directions for later work, not requirements for
the current slices and not implemented capabilities:

- **Display-specific overrides:** optionally choose different game settings
  for a TV and monitor, with explicit inheritance and stable display matching.
  Define how those overrides interact with application profiles before adding
  another configuration layer.
- **Fullscreen presentation of windowed games:** optionally select a game
  window and enlarge it across an output while preserving its smaller supplied
  buffer. This needs its own input, focus, dialog and restoration design; the
  required geometry extension still targets fullscreen content.
- **Sharpening at native resolution:** optionally run sharpening without
  enlargement. Keep it explicitly enabled and explain its processing cost and
  possible loss of direct scanout. Native-resolution bypass remains the default.

### Launching through a launcher’s own options

Requiring a Steam launch-option wrapper or a command prefix in another launcher
is outside the agreed scope, even if the executable ships in the Debian package.
There is no `kwin-upscale-run` executable. The installed effect acts within the
existing KWin session on applications the user starts normally.

### Selecting the game

Resolution changes require a matching enabled profile, shipped or user-created.
The editor can obtain a window’s identity through KWin’s interactive selection
and save a profile with its desired resolution. Session-only selection is not
implemented. Match native Wayland windows by their application
ID and Xwayland windows by their window class and instance. KWin exposes these
through its window objects. Titles are optional refinements, not the primary
identity, because they can change during play.

An editable allowlist of profiles is preferable to a mandatory catalogue of
games. A fullscreen window alone does not identify a game: browsers and video
players can also be fullscreen. Desktop-entry categories or Steam identifiers
can suggest a match but must not silently authorize resolution changes.
[Proton's X11 driver](https://github.com/ValveSoftware/wine/blob/proton_10.0/dlls/winex11.drv/window.c)
uses `steam_app_<SteamAppId>` as its window class when that environment value
is present. This is a useful identifier, not a guarantee for every game or
launcher. Distinguish the main game window from launchers, dialogs and overlays,
and allow selection for one session when its identity is ambiguous. Profiles
must not depend on Linux process inspection.

### Advertising a smaller fullscreen resolution

Investigate whether a selected game can be told that its fullscreen target is,
for example, 1920 × 1080 while the physical output remains 3840 × 2160. A
separate virtual output is not required if per-game information alone produces
the desired buffer and correct presentation. The control must leave other
applications' display information, the monitor mode and desktop scale alone.

There are three distinct sizes: advertised display modes, configured window
geometry and committed buffer pixels. Changing one does not guarantee a change
in the others or in the game's internal 3D rendering. Some games cache display
modes before their first window appears; determine whether a launch-time
mechanism or restart is necessary before claiming automatic control.

For native Wayland, investigate a preferred fractional surface scale first:
it can request fewer buffer pixels while preserving fullscreen logical
geometry and input coordinates. It is a client hint, not enforcement or a
replacement for advertised modes. Express the request relative to logical
surface dimensions, accounting for desktop scale. Changing only `wl_output`
mode information is insufficient to establish a coherent override: clients
also receive logical output geometry, surface scale and fullscreen configure
events. An implementation must keep these consistent and restore KWin's normal
policy after deactivation or output changes.

For Xwayland, the game queries the X server, which shares a Wayland connection
across X11 applications. Rewriting that connection's output information is not
a per-game solution. Xwayland's
[RandR implementation](https://github.com/mirror/xserver/blob/master/hw/xwayland/xwayland-output.c)
already supports per-client mode emulation, but associates a mode request with
the requesting X client. Running `xrandr` separately does not select an emulated
mode for the game. KWin's
[RandR integration](https://github.com/KDE/kwin/commit/bc5a2002e9afb78b336e9ea2b7699015c578b2bc)
is present in the reviewed master source and absent from the reviewed 6.3.6
source. Do not assume identical fullscreen behaviour across supported versions
or treat the emulation property as a generic game-resolution setter.

#### Per-window X11 resize and fullscreen emulation

**Status:** implemented as the profile method `X11Resize`, with a shipped Tux
Racer profile and virtual-backend regression coverage on KWin 6.3.6. Real-device
acceptance remains open. Applications must handle resize requests and establish
Xwayland's per-client mode emulation; this does not universally force internal
rendering dimensions.

**Deployment rationale and upstream direction.** The immediate goal is useful
resolution control on existing KDE installations through an ordinary package
install. Requiring a patched compositor, replacement X server or special game
launch would prevent that deployment. The targeted geometry approach is an
intentional compatibility choice under those constraints, limited to explicitly
profiled applications that cooperate with it. Application rules, bounded
negotiation, restoration and isolation tests are conditions of offering it.

Isolation means that control does not change unrelated clients' windows or
resolution choices, nor the shared output modes or desktop scale. It does not
mean zero presentation or scheduling impact: the current KWin effect API's
scanout veto is session-wide, and physical multi-display acceptance remains open.
Measured isolation in covered cases must not be described as a universal proof.

If integration into KDE becomes realistic, the long-term direction is to place
negotiation and geometry ownership in KWin and expose suitable APIs to effects.
The application policy and user experience should survive that architectural
change. Clean internal placement is an upstream design discussion, not a
prerequisite for delivering the constrained external-plugin implementation.

**Integration and upstream review risk.** This is experimental use of exported
KWin interfaces, not a stable compositor API for setting client render size.
The effect takes over selected native X configure operations while KWin keeps
logical window geometry. That division must remain consistent with KWin's
placement, decorations, stacking, input and window lifecycle. Version-specific
hierarchies and fullscreen behaviour make it more fragile than a compositor-owned
negotiation interface. Successful tests establish their covered cases, not every
interaction with other window-management policies or future KWin versions.

Per-client isolation, bounded negotiation and restoration constrain the risk;
they do not establish upstream acceptance. Keeping the plugin folder compatible
with KWin's build and style is a packaging/design property, not evidence that
maintainers endorse the interception mechanism. A dedicated compositor API would
be a cleaner integration direction for upstream discussion. It is not an
available fallback under this project's current no-external-patches requirement,
and upstream acceptance remains unestablished.

An X11 window resize is a separate mechanism from Wayland output advertising.
The effect can address one managed X window through KWin's exported window
and X11 event-filter APIs. The application must then update its rendering for
the new dimensions. A smaller drawable alone is insufficient: an application
can retain its old viewport or offscreen render targets and produce a cropped
image instead of a complete lower-resolution frame.

[Extreme Tux Racer 0.8.4](https://deb.debian.org/debian/pool/main/e/extremetuxracer/extremetuxracer_0.8.4.orig.tar.xz)
provides a useful compatibility case. Its `states.cpp` resize handler recreates
the window through `CWinsys::SetupVideoMode()`. [SFML's X11 implementation](https://github.com/SFML/SFML/blob/2.6.2/src/SFML/Window/Unix/WindowImplX11.cpp)
then requests the fullscreen mode on the application's own X connection.
This can trigger Xwayland's existing per-client emulation without changing
another client's mode or editing game settings. The mechanism is a standard
window resize; calling game-specific functions or automating the game's menus
is not part of the implementation.

Two independent conditions govern fullscreen presentation. The application's
connection must have an emulated mode, and its native X geometry must match
that mode at the output origin. [Xwayland's window implementation](https://gitlab.freedesktop.org/xorg/xserver/-/blob/xwayland-24.1.6/hw/xwayland/xwayland-window.c)
then establishes a viewport from the smaller buffer to the output and adjusts
its input coordinates. Writing `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` does not
establish that internal per-client mode: the property reports server state.

KWin 6.3.6 normally configures a fullscreen X window to the full output size,
which can make a resizing application recreate its window repeatedly. An
effect can intercept the selected window's fullscreen request,
retain KWin's fullscreen state and logical geometry, and configure the native
X frame, wrapper and client to the requested smaller size. Recent KWin manages
the application window directly; the geometry helper handles both layouts.
The controller restores KWin's normal native geometry when releasing a window,
preserves unrelated EWMH states and stacking requests, and watches replacement
windows, output geometry and Xwayland scale changes.

The demonstrated fullscreen sequence uses only exported KWin APIs and X11
window operations:

1. Resolve the selected managed `X11Window` and the requested native pixel
   dimensions from effect configuration. Match replacement windows belonging
   to that selected application as well; a client may recreate its window
   while handling a resize.
2. Resize that window's native frame, wrapper and client through KWin's X
   connection. Keep the frame at the output origin and account for Xwayland's
   coordinate scale. The request must not change the shared output or another
   client's geometry.
3. Use `X11EventFilter` to handle the selected window's
   `_NET_WM_STATE_FULLSCREEN` request and later `ConfigureRequest` events.
   On KWin 6.3.6, `blockGeometryUpdates()`, `setFullScreen(true)` and
   the no-argument `unblockGeometryUpdates()` retain KWin's fullscreen state
   without flushing its full-size native configure. The effect then sends
   the smaller native geometry itself. The boolean `blockGeometryUpdates(false)`
   overload flushes geometry and is not equivalent to that sequence.
4. Let the client's resize handler update its rendering and request its own
   RandR mode. Observe both the supplied buffer and full-output destination
   before reporting success. Neither a successful X configure request nor
   the emulation property alone proves that rendering changed.
5. Track affected windows with guarded references. Stop intercepting events
   and restore their normal geometry when control is disabled or the effect
   unloads. The demonstrated fullscreen restoration causes the client to
   recreate its normal-resolution window.

Initial negotiation waits for the application's first buffer: a resize during
window construction can be consumed before the renderer starts. A replacement
window already carrying the requested emulated mode can be intercepted earlier.
Negotiation allows at most six window replacements per process/profile/output
attempt and checks the supplied buffer, logical destination and output-specific
emulation after three seconds. Clients can discard a resize during a loading
transition, so one failed attempt restores normal geometry before retrying.
If the retry fails, normal geometry is restored and that request stays refused
while a matching window remains, unless settings are reapplied. After window
closure, a three-second quiet period preserves state for prompt XID replacement,
then discards keys with no matching window. PID is only a grouping hint: a
same-key launch within that grace period shares the refusal. Later launches
must not inherit an indefinitely cached failure. During negotiation a client
can temporarily show a smaller unscaled image. Status reports the requested size separately from the
buffer received; it does not treat that intermediate image as successful
full-output scaling. An observed buffer still does not prove the size of every
application-owned render target.

The replacement bound counts distinct managed windows, not fullscreen toggles
on the same window. Releasing or refusing control removes the active requested
size from status; the configured wish and failure reason remain separate.

Xwayland normally serves several physical outputs in the same session. Its
RandR emulation belongs to a client connection and a server output. All requests
derive their size and position from the target window's output; they do not
change the shared desktop mode. The `X11PrimaryOutputOnly` profile constraint
guards clients whose own mode selection ignores the window's output. A profile
without that constraint can use either output if the client selects its mode
correctly. Rotated outputs and unavailable modes are not negotiated. Mixed
desktop scales, output hotplug and physical pointer confinement still require
device acceptance.

| Required constraint | Mechanism and demonstrated scope |
| --- | --- |
| Controlled from the plugin | Target selection, requested size and X11 operations reside in the effect; no application-specific function calls are needed. |
| Only the selected application changes | Native geometry requests address its windows. The application makes its own per-client RandR request; the unrelated fullscreen test application remains at normal resolution. |
| No patches to KWin or Xwayland | The method uses exported APIs and existing Xwayland mode emulation on the distribution's unmodified packages. |
| No user game reconfiguration | Games start normally. Only the effect's configuration changes; the method does not edit game settings or require a resolution launch argument. |
| Enabled through Debian installation | The controller is compiled into the ordinary effect, and the package installs the Tux Racer profile with its defaults. No injected library, launcher wrapper or replacement server is needed. |

The delivery mechanism needs no replacement compositor or X server. A
research Debian package containing an ordinary installed KWin effect and its
configuration defaults demonstrated 1080p and 1440p Tux Racer buffers on
unmodified KWin 6.3.6, with full-output presentation and an unrelated
fullscreen X11 application remaining at 4K. Disabling and unloading the
research effect restored the target to 4K. These establish feasibility of
plugin delivery and a basic lifecycle, not acceptance of the production
effect or its package. Production support must retain the distinction
between a requested size and the dimensions actually supplied by the client.

Compatibility depends on client behaviour. SFML validates fullscreen sizes
against its mode list; arbitrary percentages are not automatically available.
SDL delivers X11 resize events to the application, but that alone does not
prove that a game rebuilds its renderer or requests an emulated mode. Clients
which ignore resize events need a different method or an unsupported result.
Neither an X11 resize nor a private display can enforce the dimensions of
arbitrary application-owned render targets.

Acceptance for an effect implementation must configure the desired size in
the effect, start unmodified Tux Racer through Xwayland, and verify its actual
render dimensions and KWin's received buffer. While it remains open, start a
different unmodified fullscreen X11 application and verify that it renders
and supplies buffers at the normal output resolution. Repeat for more than
one requested size and a second targeted application using a different
toolkit; include a client which ignores requests. A test-only effect does not
replace acceptance of the production effect, its package or physical input
and presentation.

For lifecycle acceptance, keep both applications running while changing the
effect's request from 1080p to 1440p, disabling control, re-enabling it, and
unloading the effect. Verify restoration to the normal resolution after
disable and unload, and verify that the unrelated application stays at its
normal resolution throughout. Use actual buffers and render traces as
evidence; exclude reconstructed trace state from rendering observations.

#### Borderless windows and Gamescope's approach

Borderless eligibility is implemented for both X11 and native Wayland. The
window must match an enabled profile, be a normal undecorated window, and have
its content and frame exactly cover one output at that output's origin. Equal
dimensions alone are insufficient. Ordinary smaller windows and windows
spanning outputs do not qualify. The existing opacity, surface, aspect-ratio
and buffer-size checks still apply.

A Wayland client must retain that logical area while supplying a smaller
buffer, for example through a viewport or a supported scale policy. The X11
resize controller additionally requires the client's mode emulation to preserve
full-output presentation and input mapping. A borderless client which only
shrinks its drawable is restored and reported as unsupported. Extending support
to such clients still requires a complete placement and input design; a paint
transform alone does not provide it. The effect does not change a game's own
fullscreen/windowed preference.

[SuperTux's SDL event handling](https://github.com/SuperTux/supertux/blob/v0.6.3/src/supertux/screen_manager.cpp)
and [video configuration](https://github.com/SuperTux/supertux/blob/v0.6.3/src/video/sdlbase_video_system.cpp)
provide an independent compatibility example: resize events update its window
dimensions and reapply video configuration. A prototype effect obtained both
4K and 1080p borderless buffers and matching presentation viewports without
game-setting edits. The observed internal framebuffer remained 1368 × 769,
illustrating why changing a window buffer does not necessarily reduce every
render target or imply a proportional performance improvement.

[Gamescope's X window manager](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/steamcompmgr.cpp)
uses ordinary `XResizeWindow()` calls to enforce the dimensions of its focused
fullscreen game window. Its broader compatibility also relies on controlling
the display that the game sees: [its Wayland server](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/wlserver.cpp)
creates private Xwayland servers and headless outputs at the nested render
size, and [its launcher](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/main.cpp)
sets `DISPLAY` to that server before launching clients. Rewriting the shared
desktop Xwayland output from this effect would not reproduce that isolation.
A running X connection cannot simply be reassigned to the private server.

The reusable design is to keep the requested render size, actual committed
buffer and displayed geometry separate, and derive composition and input
transforms consistently. Gamescope's renderer explicitly handles games which
retain a larger swapchain in a smaller borderless window, so its resize policy
must not be read as proof of reduced rendering. Its optional
[Vulkan WSI layer](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/layer/VkLayer_FROG_gamescope_wsi.cpp)
can expose the X window extent and request swapchain recreation, but operates
inside the application and is not an effect-only API for arbitrary OpenGL,
Vulkan or internal render targets.

#### Other negotiation boundaries

If a virtual output is needed, creation alone is not acceptance. Establish
game placement and display selection, mapping to the physical output, input
coordinates and pointer confinement, focus and overlays, colour descriptions,
and presentation timing driven by the physical output. Likewise, enlarging a
small window in an effect does not automatically enlarge its input region.

Experiments with unmodified KWin establish a limited native control path:
`Window::setNextTargetScale()` can produce a smaller committed buffer from a
cooperative fractional-scale client while preserving fullscreen geometry.
It does not enforce that size: the tested Qt Widgets client retained its 4K
buffer. Sending only a smaller current/preferred output-mode event also left
that client's buffer unchanged. The test Xwayland client's own RandR request
produced a 1080p buffer with a 4K destination on neon, but a 4K buffer on 6.3.6.
That early test did not use the implemented X11 resize controller, which now
provides a cooperating-client path on 6.3.6. These observations alone do not
establish real-game or physical-display acceptance.

No universal forcing mechanism has been established. Both client types remain
required; an unsupported resolution request must be reported as such, with
in-game guidance and continued scaling of eligible buffers. Acceptance must
measure actual committed sizes and exercise native Wayland and Xwayland games,
including borderless fullscreen, ignored requests and mode changes. HDR and VRR
remain requirements for any selected route.

### Wine, Valve Proton and launch-time control

Support includes upstream Wine as packaged by Debian Trixie and official Valve
Proton, not only native Linux applications or third-party Proton variants.
For the 2026-09-18 investigation, these targets are Wine 10.0
(`10.0~repack-6`) and Valve Proton 11.0-2, the latest stable release listed by
[Valve](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-2).
Record the runtime version and actual display driver in every result. A Wine
application using `winewayland.drv` is a native Wayland client; one using
`winex11.drv` is an Xwayland client in our target session. Do not assume that
Valve Proton exposes the same driver options as GE-Proton.

Windows display APIs add another layer between the game and KWin. A successful
`ChangeDisplaySettings` call or an emulated Windows desktop size does not prove
that a smaller buffer reaches this effect. In particular, Valve's reviewed
[Vulkan presentation implementation](https://github.com/ValveSoftware/wine/blob/dc26e61847081a1b5cb0733dc30feba6ee575482/dlls/win32u/vulkan.c)
contains a fullscreen-hack path that can create host-sized images and perform
its own blit. Measure the game-visible mode, presentation buffer and KWin
surface independently, including Vulkan/DXVK and D3D12/vkd3d-proton. An ordinary
GDI test window does not establish those rendering paths.

Existing software provides several relevant approaches:

| Software | Mechanism and relevance |
| --- | --- |
| [Gamescope](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/README.md) | A private display environment separates game resolution from presentation resolution. It supports Xwayland and optionally native Wayland clients. Its Wayland backend with linear filtering forwarded smaller buffers as subsurfaces in our experiments. Other compositing paths can give the host an already enlarged image; backend and actual buffer inspection are essential. |
| [Sommelier](https://chromium.googlesource.com/chromiumos/platform2/+/3d7104654150b0759fbdeb271148ba8da81f5a23/vm_tools/sommelier/README.md) | A protocol-aware proxy delegates composition to the host and translates output dimensions, configure sizes and coordinates. It supports native Wayland and separate Xwayland instances. Direct-scale experiments forwarded smaller buffers for selected native and Xwayland clients. Compatibility failures remain; gaming, HDR and synchronization acceptance is incomplete. |
| [waywall](https://tesselslate.github.io/waywall/01_options_window.html) | A nested compositor for Minecraft supports explicit fullscreen render dimensions. It demonstrates another implementation of independent fullscreen resolution, not general Wine/Proton compatibility. |
| [Wine virtual desktop](https://github.com/wine-mirror/wine/blob/wine-10.0/programs/explorer/desktop.c) | A named desktop can present chosen dimensions to Windows programs. The outer window still needs correct fullscreen presentation and input mapping in KWin. It does not cover native Linux games. |
| [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom/blob/master/README.md) | Its documented `WINE_FULLSCREEN_FSR_CUSTOM_MODE` belongs to its own fullscreen-FSR implementation. This is neither a stock Valve Proton control nor proof of a smaller buffer reaching KWin. |

Window-capture upscalers such as
[linux-rt-upscaler](https://github.com/baronsmv/linux-rt-upscaler) operate on X11
or Xwayland windows and enlarge captured content. They do not provide a general
native Wayland display override. Frame generation alone, including lsfg-vk,
does not solve fullscreen display enumeration.

Trixie Wine probes illustrate these limits: a named Xwayland virtual desktop
made Windows report 1080p and supplied a 1080p buffer, but its outer KWin window
was not fullscreen. Forcing that desktop fullscreen enlarged its supplied
buffer to 4K while Windows still reported 1080p. A native Wayland GDI mode change made Windows report 1080p,
but supplied a 3840 × 2304 backing buffer for a 4K destination. These are test
client results, not game acceptance or measurements of the Vulkan paths.

A launch-time protocol proxy is therefore an explicit research candidate,
separate from the effect's rendering code. It would advertise the desired game
size consistently before the game enumerates displays, translate fullscreen
configure and input coordinates, and forward original buffers with a viewport
mapping to KWin. X11 clients would use a dedicated Xwayland server rather than
rewriting the shared desktop server's output information. The effect remains
responsible for final enlargement on the physical output.

This is a proposed product architecture with experimental buffer-forwarding
evidence, not an implemented or accepted product solution.
Check buffer forwarding and lifetime, subsurfaces, popup geometry, pointer
locking and confinement, relative input, colour descriptions, explicit
synchronization and presentation feedback before adopting it. KWin remains
unpatched. A launch helper would be an additional component; an effect-only
universal resolution override remains unproven.

### Resolution-control direction after the experiments

For cooperating X11 applications, the
[targeted resize mechanism](#per-window-x11-resize-and-fullscreen-emulation)
provides a verified path from an effect on unmodified KWin 6.3.6. It can act
after the application starts and is included in the Debian package. Selected
borderless clients also qualify when their supplied surface retains a full-output
destination; physical input acceptance remains open.

The following launch-helper measurements are retained as research, not a
product direction. Starting the game through a helper violates the
[four requirements](#four-requirements-that-bound-every-route). Sommelier and
Gamescope remain sources of technique for output advertisement and input
mapping. Two helper mechanisms supplied smaller original buffers to unmodified
KWin 6.3.6:

- **Protocol proxy:** Sommelier's direct-scale mode forwarded 1080p and 1440p
  native OpenGL buffers, a 1080p native Vulkan buffer, and smaller Xwayland
  buffers, including Trixie Wine D3D11. A prototype which hides the proxy's
  compensating fractional-scale advertisement also handled Qt at both sizes
  with correct absolute pointer mapping. This prototype is not a shipped
  helper; unmodified Sommelier has failing Qt, Proton and multi-process Wine
  cases in our tests.
- **Gamescope Wayland backend:** `--backend wayland -F linear` forwarded smaller
  original game buffers as subsurfaces, including native Vulkan, Xwayland
  OpenGL, official Proton DXVK and vkd3d-proton probes. Its 1 × 1 root surface
  is not the game image. The current effect rejects surface children, so this
  requires deliberate support for the forwarded game surface and overlays.
  Enabling Gamescope's own upscaler or another compositing feature may instead
  give KWin an already enlarged image; recheck the actual surface tree.

The historical Gamescope experiment used:

```sh
gamescope --backend wayland -w 1920 -h 1080 -W 3840 -H 2160 \
  -F linear -f -- command
```

Add `--expose-wayland` for native Wayland clients. These flags were exercised
with Gamescope 3.16.22 from Debian Trixie backports; they document an out-of-scope experiment, not setup instructions or a path
accepted by the current effect.
The helper must start before display enumeration. A private display must be
associated with the selected game profile, including child processes; the
host-facing wrapper identity alone is insufficient to distinguish games.

Do not promise an exact rendering resolution for every program. A deliberate
fixed-4K client still submitted 4K through the smaller virtual display, and
internal render targets remain application-owned. Rejecting such buffers could
prevent presentation but would not make the game render less. Offer automatic
in-session negotiation where verified, and report unsupported cases otherwise.
In-game guidance is optional, not a substitute for the automatic-control requirement. Show **target not reached** when observation does
not confirm the requested size; continue scaling eligible actual input.

These measurements establish mechanisms, not product acceptance. For the
implemented in-session paths, relative input and confinement, fullscreen/mode
transitions, overlays, synchronization, HDR, VRR and real-game TV acceptance
remain required. Helper integration is excluded. The experimental commands do not measure performance savings.

## Rendering and lifecycle requirements

EASU replaces the enlargement step and must receive the original buffer, not
an image already scaled to the destination. Selection permits one eligible
window per output, with the full buffer visible and no buffer transform.
Fullscreen windows and explicitly profiled, undecorated borderless windows
covering exactly one output can qualify. Separate outputs select independently;
multiple eligible candidates on the same output use normal KWin rendering there.
Eligibility uses physical pixel sizes and capabilities rather than fixed
resolutions or GPU vendor checks.

RCAS is initially off and must have a real bypass: AMD's numeric zero means
maximum sharpening. Separately composited overlays and the cursor retain
KWin's normal rendering; an in-game HUD is already part of the game buffer.
Intermediate textures are reused without CPU readback. FP32 arithmetic alone
does not establish HDR correctness; formats and colour conversions must
preserve range and precision as well.

Resizing, output changes, deactivation and resource failures must preserve
normal rendering without stale textures. Release redirection and additional
direct-scanout restrictions when the effect becomes inactive. KWin owns
presentation timing; no frame timer or continuous repaint loop is introduced.

## Supported scope and full acceptance

Requirements here are validated against two separate gates. Both are real; the
difference is what each one authorises.

**Supported scope** is what a given release claims to do. A release may ship
when every case inside its declared supported scope is verified on the minimum
supported KWin, every case outside it falls back to ordinary KWin rendering,
and the settings status and documentation name the excluded cases explicitly.
A narrow supported scope is an acceptable release. A supported scope that is
wider than the evidence is not, and neither is silence about the difference.

**Full acceptance** is the complete requirement set in this handbook: native
Wayland and Xwayland, Valve Proton and standalone Wine across the required
graphics paths, HDR, VRR, the benchmark matrix and real-device acceptance.
It authorises describing the requirement itself as met.

| | Supported scope | Full acceptance |
| --- | --- | --- |
| Authorises | Publishing a release | Calling a requirement met |
| Evidence | Every declared case verified; excluded cases fall back and are named | Every required case in this handbook verified, including hardware |
| Unverified case | Excluded from scope and reported as unsupported | Blocks the requirement |

Moving a case out of a release's supported scope never deletes its requirement.
The case stays in this handbook, keeps its acceptance criteria and keeps its
owning slice open. A release note and the settings status must both say that
the case is unsupported in that release; a requirement quietly dropped between
releases is the failure this separation exists to prevent.

Each slice states both gates: the supported scope it can close against, and the
full acceptance that keeps its requirement open. A slice document is retained
while either remains incomplete, and blocked hardware cases are recorded as
blocked rather than counted as passed.

## Validation requirements

An effect that loads is not an effect that works. A plugin can be discovered,
reported supported, instantiated, installed and covered by passing tests while
never processing a single frame. Treat the scaler's effectiveness as its own
result, established only by observing that the destination pixels differ from
ordinary KWin scaling for a buffer the effect accepted.

Every rejection must name the condition that caused it. A window that meets the
documented eligibility rules and is refused anyway is a defect whether or not
the fallback renders correctly, and a status message that restates the rules
without identifying the failing one cannot diagnose it. Automated coverage must
be able to reach the refusal: a suite that passes against the same case that
fails on real hardware has a coverage gap in addition to whatever defect it
missed.

Validate original-buffer pixel mapping and lifecycle behaviour against KWin's
virtual backend, and image quality, HDR and VRR on the real output with a real
game. Compare ordinary KWin scaling, EASU, and EASU with RCAS using identical
input. Measure the whole rendering path, including the cost of losing direct
scanout, rather than timing the shader alone.

Record actual results and outstanding checks in the corresponding slice
document. An SDR prototype, a documentation check or a configured VRR setting
does not establish completion of the required HDR and VRR support.

### Isolation and compatibility acceptance

Every claimed application/runtime combination must pass two gates: the selected
application receives and benefits from the intended scaling, and unrelated
applications retain their normal resolution, window geometry and input behaviour.
The long-term goal includes every game that benefits, including Wine and Proton.
A finite test matrix establishes only its recorded combinations, not universal
game compatibility. Expand it when a new toolkit, rendering path or failure is
found, using an open-source reproducer and source inspection where possible.

For isolation, compare the plugin unloaded, loaded but disabled, a Native opt-out
profile, and active scaling. Run ordinary desktop applications and unrelated
fullscreen and borderless X11 and Wayland clients alongside the target, including
a second display with an explicit Native rule. Reverse launch order and exercise
multiple instances, focus changes, target changes, fullscreen transitions,
output moves, mixed scales, hotplug, crashes and plugin unload. Check restoration
and shared output mode/scale state as well as the actual buffers and input of
each client. On each output independently, cover pixel counts below, equal to
and above the configured minimum, and the disabled threshold.

Resolution and window-state isolation do not imply zero presentation cost:
KWin's current effect scanout veto is session-wide. Measure composition, frame
times and presentation on the other output during active scaling and disclose
any regression. A smaller submitted buffer alone also does not prove less GPU
work when a game retains fixed internal render targets. Use the performance
protocol below to establish the claimed benefit.

Compatibility coverage includes native X11 and Wayland, standalone Wine and
Valve Proton, OpenGL and Vulkan, and the required Direct3D translation paths,
in fullscreen and full-output borderless modes. Record game and runtime versions,
graphics backend, KWin/plugin revision, GPU/driver, output layout and effective
rules with the existing per-run evidence. Report each combination as:

| Status | Required evidence |
| --- | --- |
| Supported | Scaling, input, lifecycle and isolation gates passed for the stated combination and conditions. |
| Limited | Those gates passed only under named restrictions; excluded conditions remain explicit. |
| Unsupported | A reproduced failure prevents the required behaviour with available methods. |
| Untested | Evidence is missing; no compatibility claim is made. |

These evidence labels distinguish known failures from missing tests; both remain
outside a release's declared supported scope. Shipping an application profile
does not by itself establish compatibility. Each known gap needs a reproducible
case, source-led investigation and a regression test for any resulting fix.

### Test applications and progression

Establish a reproducible baseline with small, open-source applications before
testing games from Steam or Epic Games Store. Use this initial application set
in order; the list is a test plan, not a record of passing runs:

| Stage | Application | Purpose and required selection |
| --- | --- | --- |
| 1: OpenGL benchmark | [glmark2](https://github.com/glmark2/glmark2) | Compatibility and regression cover for a scale-driven OpenGL client: the window still covers the output, the image is right, an unrecognized client is untouched, and nothing gets worse. Use it for performance comparisons only with a scene whose frame time was shown to respond to resolution; most of its scenes do not. Test Wayland and X11 builds separately, with X11 through Xwayland. |
| 2: Vulkan benchmark | [vkmark](https://github.com/vkmark/vkmark) | The same for a Vulkan client that takes its size from the advertised mode and declares the scale separately, and the only test here committing 16-bit-per-channel buffers. Its `effect2d` scene does respond to resolution, so it can also carry the performance comparison. Explicitly select Wayland and XCB in separate runs and keep presentation mode consistent within each comparison. |
| 3: Simple OpenGL game | [Extreme Tux Racer](https://sourceforge.net/projects/extremetuxracer/) | Test game identification, selection and effect-driven resolution reduction with a simple OpenGL game. Verify the actual buffer change, fullscreen coverage and input. |
| 4: Open-source Vulkan game | [SuperTuxKart](https://supertuxkart.net/) | Test the same game-identification and resolution-control path with Vulkan. Select `--render-driver=vulkan` explicitly and confirm it in the log. |
| 5: Store-game acceptance | Selected games from Steam and Epic Games Store | Proceed after stages 1–4 pass. Exercise Valve Proton and standalone Wine separately; select titles that cover the required Windows graphics paths, HDR and VRR, and record the exact game/runtime combinations. |

Windows builds of suitable open-source applications can provide additional
Proton and Wine checks before the store-game stage. They complement the native
runs; one native Vulkan game does not cover Direct3D translation.

SuperTuxKart documents the Vulkan selector in its
[1.4 release announcement](https://blog.supertuxkart.net/2022/09/supertuxkart-14-release-candidate-1.html).
That release describes the renderer as experimental; verify that the actual
distribution build includes it and does not fall back to OpenGL. Application
choice alone never proves which graphics or window-system backend ran.

Both benchmarks document `--fullscreen` as equivalent to `--size -1x-1`:
[glmark2's manual](https://github.com/glmark2/glmark2/blob/master/doc/glmark2.1.in)
and [vkmark's option parser](https://github.com/vkmark/vkmark/blob/master/src/options.cpp).
Do not treat `--fullscreen --size 1920x1080` as a verified way to obtain a 1080p
buffer covering a 4K output. Establish a working lower-buffer-size fullscreen
route for each backend and record it. A native-size fullscreen run is useful
for checking bypass and basic stability, but cannot pass the scaling check.
Use the compositor's Wayland/Xwayland paths; a direct KMS benchmark bypasses
KWin and cannot validate this effect.

For each application/backend combination, first keep SDR and a fixed refresh
rate to isolate the scaler. On the 4K output, test actual 1920 × 1080 and
2560 × 1440 input buffers, plus native 3840 × 2160 as the bypass control.
Compare effect disabled, EASU with RCAS off, and EASU with RCAS on against the
same scene and input size. Confirm the committed input and destination sizes
and evidence that EASU rendered a frame; an enabled checkbox is insufficient.
Exercise fullscreen/windowed transitions, focus changes, resolution changes,
effect deactivation, cursor and overlays. Follow the performance measurement
protocol below; keep presentation settings identical within each comparison.

A stage passes when its required scaling and bypass cases work, image and
input checks pass, and lifecycle changes restore ordinary rendering. Record
failures and unavailable cases explicitly; do not advance by counting a launch
or a native-size buffer as successful upscaling. Benchmark stages also require
the measured performance differences and their assessment. The game stages
require correct identification and effect-driven resolution reduction as
specified below; manually changing the game's settings does not pass that
requirement. A visually correct run alone is insufficient. Follow the baseline with SDR
on an HDR output, VRR during scaling, and native HDR/VRR combinations using
applications that actually support those paths. Lack of HDR content in the
initial test games leaves native HDR acceptance for suitable later titles.

Every run records application/package version, launch arguments, scene,
graphics API and window backend, runtime/translation versions where relevant,
output mode and desktop scale, actual buffer and destination sizes, effect and
RCAS state, colour/HDR and presentation settings, observed image/input results,
frame-time measurements and remaining limitations. Keep these results in the
slice document, with temporary binaries and check caches under `build/`.

### Game identification and resolution-control tests

The primary purpose of Extreme Tux Racer and SuperTuxKart is to test the
effect's game identification and resolution reduction. Image quality, input
and lifecycle checks accompany these tests. The benchmarks above provide the
controlled performance comparisons.

The implementation recognizes editable application profiles, negotiates selected
Wayland output information or X11 window sizes, and scales eligible supplied
buffers. The X11 method includes Tux Racer on the primary output; unsupported
client behavior remains a reported limitation. These tests remain acceptance
requirements: virtual-backend results do not establish image quality, input,
GPU import or lifecycle acceptance on real hardware.

| Test | Required observation |
| --- | --- |
| Identify the game | Observe the real native application ID or Xwayland window class/instance, distinguish the main game window from launchers and dialogs, and associate it with the user's selected game. Repeat after restart and title changes; verify stored matching if a profile is used. |
| Select the target | Apply resolution control only to the selected game. Other games, launchers, browser/video fullscreen windows and the desktop must retain their normal resolution policy. Fullscreen eligibility alone must not count as game identification. |
| Reduce resolution | Start with a measured native-size game buffer. Through the effect's control, request 2560 × 1440 and 1920 × 1080 on the unchanged 3840 × 2160 output. Observe smaller game buffers and actual upscaling across the full output, with correct input coordinates. |
| Confirm or reject the request | Distinguish desired size, a sent request and the committed buffer. An ignored or adjusted request must show the actual result without repeated requests or an apply-success claim. An ignored request exercises fallback but does not pass the reduction case. |
| Change mode or output | Exercise fullscreen/windowed transitions, desktop-scale and output changes; recompute the intended pixel size and preserve focus, pointer confinement and input mapping. |
| Restore normal policy | Disable control, deselect the game and close/restart it. Restore KWin's normal scale/output policy without stale overrides, forced screen modes or continuous repainting. Record the client's actual response to restoration separately. |

Manual in-game resolution changes may establish comparison baselines or help
diagnose a failure. They do not demonstrate that our identification and control
path caused the reduction. Likewise, shrinking an already completed native-size
frame is not a successful resolution reduction. If a game needs a launch-time
setting outside this plugin, record the compatibility gap instead of claiming
automatic control. A bind-time method applies on the next normal game launch.

Record each game's actual window-system backend. Validate native Wayland and
Xwayland separately on the minimum supported KWin; successful control of the
cooperative test client does not establish control of these games. Once these
open-source game cases and the benchmark stages pass, repeat identification,
reduction and restoration with the selected Steam/Epic games under Valve Proton
and standalone Wine. An unavailable control path remains an implementation or
integration gap in this acceptance stage.

### Benchmark performance comparisons

#### An instrument has to respond to resolution before it can measure this

A benchmark can only measure what reducing the rendering resolution is worth if
its own work scales with that resolution. glmark2's `terrain` scene does not,
and this was read in its source rather than inferred from its numbers: in
`src/scene-terrain.cpp` the height and normal maps are fixed at 256 × 256, the
specular map at 512 × 512 and both bloom passes at 256 × 256, and only the
terrain pass itself uses the canvas size. That pass draws
`make_grid(256, 256, …)`, roughly a hundred and thirty thousand triangles, so
what does scale with the window is bound by geometry rather than by fill.

Measured on 2026-09-18 on a 3840 × 2160 screen, windowed so that nothing in
this effect took part: `terrain` ran at 21 frames per second at 1920 × 1080 and
26 at 3840 × 2160, which is to say a quarter of the pixels made it slower.
`refract` and `texture` were unchanged, `shading` and `desktop` moved by six and
sixteen per cent. vkmark's `effect2d` does respond, and its source says why: its
render area is the swapchain extent and its kernel steps are `1 / extent`, with
no fixed-size intermediate anywhere. It went from 20.8 ms a frame at 1920 × 1080
to 27.0 ms at 3840 × 2160.

**Before using any scene to measure this effect, run it windowed at two
resolutions with the effect uninvolved and confirm that its frame time
responds.** A scene that does not respond cannot show a gain, cannot show a
loss, and will read as though the effect achieved nothing.

#### What the benchmarks are for instead

They remain required, for two purposes that do not depend on their scores.

They are **compatibility tests**, and each covers ground the games do not.
glmark2 is a scale-driven OpenGL client whose buffer is the logical size times
the advertised scale; vkmark is a Vulkan client that takes its size from the
advertised mode and declares the advertised scale separately. Between them they
exercise both remaining control methods and both ways a client can be told.
vkmark also commits 16-bit-per-channel buffers, `DRM_FORMAT_XBGR16161616`, which
is how a gap in the scaler's readable formats was found on 2026-09-18: every
frame of it was being refused, and nothing else in the test set had shown that.

They are **regression tests against making things worse**. The effect must not
reduce the presented frame rate or lengthen the slow tail of frame times for a
client it cannot help, and must leave one it does not recognize untouched. With
presentation now measured, that is checkable rather than a matter of opinion.

#### Measuring what reducing the resolution is worth

Their functional checks establish that each measurement exercised the intended
path. Determine both the net benefit of lower-resolution rendering
with upscaling and the additional cost of EASU and RCAS over ordinary KWin
scaling. Do not require or assume a speedup before measuring it.

Keep the physical output at 3840 × 2160 and run this matrix for each selected
scene and window-system backend. Repeat B–D at both 1920 × 1080 and 2560 × 1440
actual input resolution:

| Run | Actual input | Effect state | Comparison purpose |
| --- | --- | --- | --- |
| A0 | 3840 × 2160 | Disabled | Native-resolution performance baseline |
| A1 | 3840 × 2160 | Enabled, native-size bypass | Inactive-path overhead relative to A0 |
| B | 1920 × 1080 or 2560 × 1440 | Disabled; normal KWin presentation/scaling | Performance of lower-resolution rendering without this effect |
| C | Same input as B | EASU, RCAS off | Net benefit relative to A0 and extra cost relative to B |
| D | Same input as B | EASU and RCAS at a recorded, fixed strength | Added sharpening cost relative to C |

Report average application FPS and frame-time distributions, including median
and 95th/99th percentile frame times where frame traces are available. Include
the benchmark's scene results, not just its aggregate score. Distinguish the
application's reported render throughput from frames actually presented by
KWin; discarded or queued frames are not additional displayed frames. Record
GPU time for the application and compositor where instrumentation permits,
and do not label CPU submission time or a benchmark score as total GPU time.
State unavailable measurements explicitly.

Use two separate measurement series:

- **Throughput:** remove the application's frame cap and avoid a presentation
  limit masking the comparison where the backend permits it. Keep the physical
  refresh rate, synchronization policy and Vulkan presentation mode unchanged
  across A0–D. Record unavoidable limits, including refresh or CPU bottlenecks;
  equal FPS at such a limit does not prove equal rendering cost.
- **Cost at a fixed frame rate:** apply the same sustainable target frame rate
  to each run. Compare GPU cost, frame-time stability and power/energy where
  measurable. This determines whether upscaling saves work while delivering the
  same frame rate. Keep this series separate from uncapped throughput results.

Warm up the selected scene and shader caches before collecting samples. Use
the same scene parameters, quality settings and measurement duration, and run
each case at least three times. Alternate the baseline and effect runs to
expose drift; record thermal/power state and competing load. Report the median
of repeated measurements and their spread, with absolute and percentage
differences for C versus A0, C versus B, D versus C and A1 versus A0. Changes
within run-to-run variation are inconclusive. Compare results within one
benchmark/API/backend; glmark2 and vkmark scores are not interchangeable units.

Record composition versus direct scanout for each case. The end-to-end result
must include the cost of losing direct scanout when enabling the effect. If a
controlled comparison with composition in both cases is available, report it
separately to help isolate filter cost. A shader-only timing cannot replace
the end-to-end comparison. Complete the initial measurements in SDR at fixed
refresh, then repeat the relevant comparisons with HDR and VRR as supported.

## Automated quality gates

The plugin requires at least **90% executable C++ line coverage** on the
minimum supported KWin version. The denominator includes all plugin translation
units and their executable headers, including unexecuted code. Generated code,
tests, build information outside the plugin and GLSL are excluded. The shader
tests check rendered pixels separately; C++ coverage does not measure shader
branches. Scripts require smoke checks and the existing tooling regressions,
without a percentage target.

`.pre-commit-config.yaml` defines the checks. Commit checks include Hadolint for
both Containerfiles, Bandit for Python security patterns and Gitleaks for staged
secrets. Push checks also scan the complete Git history with Gitleaks; CI fetches
full history. The existing clang-tidy configuration includes Clang's security
and bug analyzers. Findings fail the check. Container package versions follow
the distribution so security updates remain available; checker versions are
pinned.

For PRs and pushes to `master`, `tools/ci_scope.py` selects a reduced path only
when every changed file is Markdown at the repository root, under `doc/` or
under `.github/`. It includes deletions and both sides of renames. Unknown paths,
mixed changes, empty diffs or unavailable comparisons retain full validation.
The `docs` check group uses pre-commit's native file filtering for both stages;
history secret scanning, REUSE and whole-tree repository rules still run.
Tooling regressions use the file patterns in `.pre-commit-config.yaml`, so they
do not run for documentation-only changes. GCC, Clang, clang-tidy, sanitizers,
coverage and packaging are skipped for that scope. The required Quality gate
accepts skips only when the successful scope job explicitly selected them.
Nightly, release and manual full runs always retain complete validation.

Inside the maintained container, use `python3 -B tools/run-checks.py docs --base
<base-commit>` for the same targeted checks; it rejects a non-documentation diff.

Code-affecting PR CI runs separate Trixie builds for GCC coverage, Clang ASan with UBSan and
leak detection, and Clang TSan. Sanitizers must not be combined with coverage
or with each other beyond the supported ASan/UBSan combination. The address
sanitizer build also runs libFuzzer against the resolution policy for 60 seconds;
nightly extends that to 600 seconds. Saved corpus inputs and crash reproducers,
CTest logs and coverage reports are uploaded even after a failed check.

For a clean coverage build inside the project container:

```sh
cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=g++ -DUPSCALE_COVERAGE=ON
cmake --build build/coverage
export UPSCALE_BUILD_DIR=build/coverage
pre-commit run upscale-render-tests --all-files --hook-stage manual
pre-commit run upscale-coverage --all-files --hook-stage manual
```

Reports are written to `$UPSCALE_BUILD_DIR/coverage/`. Use a fresh build directory
or remove its `.gcda` files before measuring a changed test suite; old execution
counts must not supply coverage for tests that no longer run. The gate also
rejects a report missing any production `.cpp` file.

For ASan/UBSan, configure a separate build with `-DCMAKE_CXX_COMPILER=clang++
-DUPSCALE_SANITIZER=address,undefined -DUPSCALE_FUZZING=ON`, then run the same
runtime hook and `pre-commit run upscale-fuzz --all-files --hook-stage manual`.
Set `UPSCALE_BUILD_DIR` to that build. For TSan use another build with
`-DUPSCALE_SANITIZER=thread`, without fuzzing. The runtime hook supplies the
sanitizer options used in CI. Clang TSan may require a container that allows
the `personality` operation, as configured in the CI job.

The lifecycle test runs a private bus and KWin virtual session, isolated from
the desktop. KWin 6.3's virtual backend cannot use OpenGL without a DRM device.
The test therefore supplies a deterministic capture renderer to the real
effect while KWin manages actual Wayland windows through QPainter. It checks
pixel mapping, the filtered destination pixel, settings changes, window and
buffer eligibility, multiple candidates, reloading and cleanup. Separate EGL
tests exercise the production shaders under desktop OpenGL and OpenGL ES.
This fixture covers the Trixie and Ubuntu package APIs; neon runs the portable
resolution, configuration and renderer tests. Neither establishes real GPU
buffer import, HDR, VRR or TV acceptance.

Distribution Qt and Mesa are not instrumented. TSan ignores intercepted
accesses originating in those modules, whose internal atomics it cannot see;
instrumented plugin and test accesses remain checked. LeakSanitizer uses
documented allocation-stack suppressions only for observed KWin 6.3 startup
globals and KF6 Config shutdown allocations in the private integration session.
It does not suppress plugin functions or entire libraries. The standalone
configuration and rendering tests retain unsuppressed leak detection.

### Security analysis

CodeQL analyses three supported language groups: the C++ effect, the
Python tooling and the GitHub Actions workflows. Its command line is pinned in
`tools/run-codeql.py` by release tag and published checksum, like every checker
version here, and the first run fetches it into `build/codeql-cli`.

The C++ database is built from this project's own CMake and Ninja configuration
inside the maintained Trixie container. CodeQL's autobuild cannot configure
against KWin and KDE Frameworks, and a hosted runner has neither, so the scan
runs where the real build already works. The generated inputs that build
produces - the build information source, the KConfig classes, the moc and
resource units and the Wayland protocol sources - are part of the database.
`autotests/resolution_fuzz.cpp` is not: it compiles only in the Clang fuzzing
configuration, which the sanitizer jobs own. Python and workflow extraction is
confined to `tools/` and `.github/`, so a scan of this repository does not
become a scan of its own build outputs and unpacked analysis tools.

`upscale-codeql` in `.pre-commit-config.yaml` is the single definition of that
check, as for coverage and fuzzing. Inside the container,
`python3 -B tools/run-checks.py codeql` runs it, and the weekly `CodeQL`
workflow runs the same command before publishing its SARIF to the Security tab.
Set `UPSCALE_CODEQL_SUITE` to select a wider query suite. The scan is scheduled
and manually dispatchable; it does not gate pull requests, and it reports what
it found rather than failing on it. Requiring it is a decision to take once its
findings have been read, not a default; `--fail-on-findings` is the switch that
turns the same check into a failing one.

Dependency review covers what GitHub's dependency graph actually resolves for
this repository, which is the pinned actions in the workflows. The build
dependencies in `debian/control`, the CMake and KDE Frameworks interfaces, the
pre-commit hook versions and the vendored shaders are not represented in that
graph and are not reviewed by it; their versions remain the maintainer's and the
distribution's responsibility. `tools/dependency_review.py` holds the policy:
only dependencies a change introduces are reviewed, advisories from moderate
upwards block, an advisory whose severity cannot be read is treated as worse
than critical, and an exception names its GHSA identifier together with the
reason it does not apply. Passing a recorded comparison with `--changes`
exercises that policy without waiting for a real advisory.

## Build and release pipeline

The public repository uses the same maintained container definitions locally
and in GitHub Actions. Inside the Trixie container, run
`python3 -B tools/run-checks.py all` to run both pre-commit stages, GCC and Clang
builds, clang-tidy and metadata validation, coverage, sanitizers and fuzzing.
Individual groups use `lint`, `gcc`, `clang`, `tidy`, `coverage`, `address` or
`thread`. This command orchestrates the existing hooks; it does not replace
their definitions. Build directories, reports and caches stay under `build/`.
ThreadSanitizer needs the container personality permission described above.

Builds use Ninja's native concurrency. Package builds use debhelper's
`cmake+ninja` backend and dpkg's automatic job count. CTest uses its native
parallel level on CMake 3.29 or newer; older supported versions remain serial.
Explicit `CMAKE_BUILD_PARALLEL_LEVEL`, `CTEST_PARALLEL_LEVEL` and
`DEB_BUILD_OPTIONS=parallel=N` settings are preserved. Pass these environment
variables into the container when limiting a local run.

Static analysis uses `run-clang-tidy`'s native worker pool. Coverage uses
gcovr's CPU-count mode. Fuzzing runs one job per libFuzzer default worker
(half the CPU cores, at least one), sharing a corpus; its time budget applies
to each job and its memory limit remains a per-process bug-detection bound.
Worker logs stay with the other reports. Pre-commit retains its own scheduling;
formatting hooks are not launched concurrently by another wrapper.

The local `all` command runs check groups sequentially, letting each group use
the machine. CI matrix jobs run on separate hosted runners. There is no fixed
two-job cap, forced RAM allocation, or project-specific resource scheduler.
These CPU-based defaults do not promise automatic protection against exhausting
RAM. Constrained environments should set the native job limits above; container
CPU allocations must also reflect the resources actually available to the job.

CI's final `Quality gate` requires every supported-platform check to succeed.
Both tagged releases and nightly publication depend on these checks for their
own commit. Nightly also builds and runs the available tests against neon with
GCC and Clang, independently of publication. Container dependencies refresh
daily; action commits and Python checker versions are pinned. Dependabot proposes
action updates weekly. Distribution package versions remain the distributions'
responsibility rather than a second list of project build dependencies.

Packaging builds twice in separate source directories with the commit timestamp
as `SOURCE_DATE_EPOCH` and a deterministic changelog entry. Main and debug
packages must compare byte for byte. The first build's `.buildinfo` and `.changes`
records accompany the deliverables. Clean distribution containers exercise
installation, reinstallation, loading the installed effect and configuration
factories with all symbols resolved, removal and purge. Loading a factory does
not construct an effect in a real KWin session. An upgrade from an older release
and actual GPU rendering remain separate acceptance cases.

The source archive is extracted, configured, built, tested and staged without
Git metadata. Publication accepts only the complete four-platform package
matrix, its build records and the source archive. Reports and fuzz corpora are
never release assets. A SHA-256 manifest covers all deliverables. The workflow
uploads a draft and downloads it again to compare every asset before publishing.
The preceding nightly remains available until that verification succeeds.
The final replacement is not atomic: after removing the previous nightly, the
publisher retries promotion three times by release ID. A persistent API failure
can leave the nightly unavailable. The failure log prints the exact command to
promote the already verified candidate; run it after service recovery. It uses
the permanent release ID so a lost success response does not invalidate retries.

A manual Nightly run defaults to `verify-only`: it builds the complete package
matrix and source archive, runs the quality gates, attests the deliverables and
verifies their provenance. The resulting `verified-release-candidate` workflow
artifact is retained for 14 days; the public nightly release is unchanged.
This mode also permits a review branch. Clear `verify-only` only when publishing
from master. Scheduled runs continue publishing changed master commits.

### Pull request reviews

CodeRabbit is connected through its GitHub App to review pull requests. Reviews
on this public repository use its [free open-source offer](https://www.coderabbit.ai/oss).
The app is managed in GitHub's installed-app settings; no model API key or CI
secret is required. `.coderabbit.yaml` enables its request-changes workflow:
actionable findings request changes; approval follows review of the latest
commit and resolution of blocking findings. Automatic code-writing features
are disabled. The repository's own checks define its documentation requirements;
CodeRabbit's generic docstring percentage check is disabled.

The `CodeRabbit approval` status verifies an actual approval by the installed
bot account for the current commit. Review completion alone does not pass it.
Require this status alongside `Quality gate`, with GitHub Actions as the
permitted source for both. The approval workflow must already exist on the
default branch when enabling that requirement; otherwise the bootstrap PR
cannot produce its required status.

PR events and an unprivileged review-event workflow wake a trusted workflow
that reads current GitHub review metadata. It executes only default-branch code
and never consumes PR artifacts. Approval of an older commit, a dismissed
approval or a change request cannot pass. Review-fetch errors leave the status
pending after invalidation; event delivery or API outages may delay updates.
Because commit statuses are shared by PRs with the same head commit, all open
PRs sharing that commit must have approval. The workflow can be dispatched
manually to refresh statuses after an outage.

The public repository protects `master`: changes go through pull requests,
the branch must be up to date with a passing GitHub Actions `Quality gate`,
and review conversations must be resolved. `.github/CODEOWNERS` assigns all
paths to `JensKSP`, including the ownership policy itself. Code-owner review is
required with zero additional approvals. For other authors, the owner's approval
satisfies the ownership requirement. GitHub does not allow authors to approve
their own PRs: zero additional approvals must not be assumed to waive code-owner
review. An owner-authored PR that needs ownership approval requires another
eligible code owner or an explicitly authorized administrator bypass.
Owner-enabled auto-merge has been verified for owner-authored PRs with these
settings and both required statuses passing. If native ownership enforcement
blocks a PR, use an explicitly approved policy adjustment; do not silently bypass
reviews or claim that self-approval is possible.
GitHub uses the CODEOWNERS file from the PR's base branch. Changes to ownership
therefore take effect after the owner merges them into `master`.
Administrators retain GitHub's branch bypass
option for owner-directed recovery; force pushes and branch deletion remain
disabled in the normal policy. Stable release tags matching `v*` cannot be
updated or deleted except through the explicit `JensKSP` owner bypass. The
rolling `nightly` tag is outside that rule so the release workflow can replace it.

Agents must not attempt any override or weaken protection without the owner's
explicit permission for the specific operation, as required by the repository
rules. Access to owner credentials is not approval. GitHub authorizes the account
making a request; separate credentials without bypass privileges are necessary
to enforce a distinction between owner and agent at the permission level.

Merging also requires explicit owner permission for the particular PR when no
override is involved. Agents may prepare and push changes, follow checks and
address reviews, but must not merge, enqueue a merge or enable auto-merge on
their own. The owner normally enables GitHub auto-merge in the web interface.

CodeRabbit approval supplements the required `Quality gate` and human ownership
review; it replaces neither. CodeRabbit's explicit `approve` and top-level
`resolve` commands can bypass its normal approval conditions and require the
owner's permission for that specific override, just like a GitHub bypass.
Investigate each finding against the code and requirements, fix valid issues,
and explain findings that do not require a change. After pushing fixes, check
both CI and review feedback for the latest revision before handing back the PR.

#### Optional repository services

Projects, Discussions, additional code owners, public build images in GHCR, manually
dispatched hardware workflows, signed commits or release tags, and community
conduct guidance/saved replies are optional future capabilities. Adopt them
when contribution volume, support needs, additional maintainers or measured
build cost justify them. They are not requirements for the plugin or the
current release pipeline. Hardware workflows would require trusted manual
dispatch and must not run arbitrary public pull requests on personal machines.
Commit/tag signing is separate from the artifact attestations below.

Repository-wide immutable releases would require a different nightly design:
the current rolling `nightly` tag and assets are intentionally replaced. Keep
the CMake version and tag-triggered publisher as the release authorities;
additional version bots and a separate documentation Wiki are not planned.

### Signing and verification

Release artifacts and their checksum manifest receive GitHub build-provenance
attestations using Sigstore and the workflow's OpenID Connect identity. There is
no personal signing key, uploaded secret or hardware token to configure. Only
the publication job receives `contents: write`, `id-token: write` and
`attestations: write`; compilation and PR checks have read-only repository access.

With a recent GitHub CLI supporting `attestation`, verify a downloaded package:

```sh
gh attestation verify ./package.deb --repo JensKSP/kwin-effect-upscale
```

For a candidate tied to a specific commit, also pass `--source-digest COMMIT` and
`--signer-workflow JensKSP/kwin-effect-upscale/.github/workflows/publish.yml`.
`SHA256SUMS` verifies the release artifacts listed in that manifest.
`provenance.sigstore.json` contains the signing bundle and is verified separately;
it is not included in the checksum manifest. Attestations identify the build's origin; acceptance
tests establish its behaviour. This signs downloaded release artifacts, not an
APT repository's metadata. An APT repository would require a separate design.

Do not enable repository-wide release immutability while the same repository
hosts the moving `nightly` release. Stable releases are never overwritten by
the publication script; a repeat publication must match the existing assets.

### Distributions beyond Debian

Proposed targets, not decided and not implemented. The pipeline builds Debian
and Ubuntu packages, while many KDE users who game are on other distributions.
A KWin effect is a compositor plugin built against the KWin the session
actually runs, so a package per distribution is the only workable delivery
form. A scripted effect could be published through the KDE Store; a C++ effect
cannot, and Flatpak does not apply to a compositor plugin.

| Target | Form | Notes |
| --- | --- | --- |
| Arch | `PKGBUILD` in the AUR | builds from the published source archive; a rolling KWin makes the minimum-version claim worth rechecking per release |
| Fedora and its KDE variants | RPM spec built in Copr | the usual route for KDE packages outside the distribution proper |
| openSUSE | spec built in OBS | OBS can build Debian formats too, which is a reason to keep the existing pipeline authoritative rather than migrating to it |

- Each target builds the published source archive unchanged. Distribution
  patches do not belong in this repository, and a recipe that needs one is a
  bug in the source archive.
- Do not describe a distribution as supported when no acceptance host runs it.
  A community-built recipe is listed as such, with its builder named.
- Record the KWin version each package was built against. The effect API
  version is the compatibility boundary, and a package built against a
  different KWin loads or fails as a unit.
- Release verification stays as specified. A target that cannot produce
  reproducible builds with a checksum manifest and provenance ships as a recipe
  rather than as a binary this project signs.

### Hardware acceptance hosts

Hardware acceptance initially runs manually on reviewed candidates on Debian:
wzpc with AMD Strix Halo and the workstation with NVIDIA RTX 5090. Record the
exact package checksum, Debian, KWin and driver versions, display and connection,
and each observed SDR, HDR, VRR and performance result in the active slice.
Untrusted PR jobs run on hosted runners, not on these desktop machines.

The NVIDIA host is `pcjensd`. What it is made of is recorded here rather than
in a slice, because the driver, the display mode and the KWin version together
decide what the effect is allowed to attempt; a result from this host cannot be
read without them, and they outlive the slice that first observed them.

| Part | pcjensd |
| --- | --- |
| Distribution | Debian 13 (trixie), kernel 7.2.6-zabbly+ |
| CPU | AMD Ryzen 9 9950X3D, 16 cores / 32 threads |
| Memory | 62 GiB |
| GPU | NVIDIA GeForce RTX 5090 (GB202, `10de:2b85`), 32 GiB VRAM |
| Driver | `nvidia-driver` 615.71.09-2, proprietary; `nvidia_drm` with `modeset=1` and `fbdev=1` |
| Display | LG ULTRAGEAR+ on DisplayPort (`DP-3`), 3840 × 2160 at 240 Hz, scale 1.45 |
| Display state | SDR; HDR disabled, wide colour gamut disabled, VRR set to never |
| Session | Plasma 6.3.6 on Wayland, KWin 6.3.6, Qt 6.8.2 |
| Compositing | OpenGL through EGL; KWin reports the context as OpenGL 3.1, GLSL 1.40 |

Two of those entries matter more than their neighbours. KWin 6.3.6 reports this
card's **GPU class as `Unknown`**, so any behaviour that depends on KWin
recognising the hardware is untested here by definition. And the display is
**driven at 240 Hz**, which leaves well under five milliseconds per frame: a
performance result from this host is a statement about that budget, not about a
60 Hz one. HDR and VRR are both off at the time of writing, so neither has been
exercised; turning either on is a change of test conditions and is recorded as
one.

The [pipeline slice](agents/slice-build-release-pipeline.md) records validation and
remaining hosted, BSD and hardware acceptance work.
