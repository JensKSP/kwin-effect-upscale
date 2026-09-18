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

Each major implementation slice has its own temporary document for its plan,
progress, findings, TODOs and test results. The current slice is
[FSR 1 with HDR and VRR](slice-fsr1-hdr-vrr.md). Completing a slice removes only
its working document; this handbook remains. Durable implementation details
also belong in source comments and tests.

Statements about KWin below were read in the source at
`v6.3.6` (commit `b8de432`, the version Debian Trixie ships as `4:6.3.6-1`) and
at `master` (commit `c1ca390`, 2026-09-13).

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
resolution control is a separate requirement under investigation: the effect
should help obtain a smaller buffer without patching KWin, then enlarge that
buffer on the physical output.

**The primary platform is a KWin Wayland session. Native Wayland games and
Xwayland games must be supported, including Windows games running through
Valve's Proton and through standalone Wine.** Xwayland
compatibility is required within the Wayland session; it does not imply a
requirement for a separate X11 desktop session. Resolution-control mechanisms
may differ between the two client types and require separate validation.

The effect acts on a window when all of this holds:

- the window is fullscreen on its output, and
- its buffer is smaller than the output area it covers, and
- the effect is enabled and a scaler is configured.

Everything else uses normal KWin rendering. A window that supplies a native-size
buffer is not scaled. The initial path creates no virtual screen and does not
alter advertised screen sizes; the resolution-control investigation below
revisits that restriction without claiming an implemented feature.

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
switchable, initially off. The implementation will start with FP32 fragment
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

Capture and optional EASU intermediates use RGBA32F. KWin's GLES texture
allocator creates 8-bit storage despite accepting a different format argument;
the compatibility layer replaces that mutable storage with RGBA32F and checks
framebuffer completeness. Shader samplers explicitly use high precision on
GLES. Failure disables processing until reconfiguration and renders normally.

The capture keeps the original render target's colour description. Changing
its transfer function could turn an identity scRGB conversion into a colour
shader operation that clips negative values. KWin performs its normal gamut
conversion and tone mapping at input resolution. EASU decodes the destination
transfer function and maps linear values, in units of reference white, into
the bounded working domain `0.5 + 0.5 * sign(c) * sqrt(abs(c)/(1+abs(c)))`.
The inverse restores the signed range before applying only the destination
transfer function. RCAS, when enabled with nonzero strength, operates in that
same working domain. This filter-domain choice is reversible for constant
colours; its HDR image quality still needs display acceptance. It is not AMD's
unchanged SDR input encoding or evidence of accepted HDR support.

Supported destination transfers are sRGB, gamma 2.2, linear and PQ. New or
unknown destination transfer functions use normal rendering. There is no frame
timer: client damage expands to a full-window repaint because both filters
sample neighbouring pixels. Scanout is blocked only while there is one eligible
candidate; KWin retains ownership of refresh and presentation timing.

## Versions

| | Version | Role |
| --- | --- | --- |
| Minimum | KWin 6.3.6, effect API `0.236` | what Debian Trixie ships; the supported target |
| Tracked | KWin git master | built in CI to catch API changes early, not a supported target |

Master renames some things the effect will touch, `QRect` becoming KWin's own
`Rect` among them. Where that matters, the difference is absorbed in a thin
compatibility layer rather than in the effect's logic.

## Configuration

Configuration follows KWin's own pattern:
`upscaleconfig.kcfg` and a page registered as `X-KDE-ConfigModule` in System
Settings. The page implements the controls below. Resolution wishes currently
remain guidance; no client resolution request or mode override is sent.
The status can be refreshed explicitly and reports supplied buffer dimensions,
not internal game rendering resolution. A saved preference is never presented
as a successfully applied client request.

| Control | Behaviour |
| --- | --- |
| Enable upscaling | Enable processing of eligible fullscreen windows; disabling restores normal KWin rendering. |
| Scaler | Show FSR 1 for the initial implementation. Offer a selector when several scalers are implemented and supported. |
| Preferred game resolution | Automatic (use the supplied buffer), or a percentage slider with a numeric percentage and live width by height in physical pixels. |
| Resolution preset | Native, Ultra Quality, Quality, Balanced, Performance, or Custom; changing the slider selects Custom. |
| Sharpening | RCAS switch, initially off, and a 0–100% strength slider. Zero bypasses sharpening; increasing the value increases strength. The UI must not expose AMD's reversed parameter directly. |
| Status | Desired input, actual supplied input, destination resolution, active scaler, and a reason when upscaling is inactive. Show HDR and VRR information only to the extent actually known. |

HDR and VRR follow KWin's display settings. They are mandatory supported paths,
not optional quality presets. An enabled VRR setting must not be labelled as
proof of currently variable presentation.

### Percentage and pixel resolution

The slider expresses the desired input size as a percentage of the covered
output's physical pixel width and height, independently of Plasma's desktop
scale. It ranges from 50% to 100% for the initial FSR path, with one-percentage-
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

Automatic is the default and makes no resolution request. Selecting Native
requests or recommends native input; processing still follows the actual
buffer until the client changes it. At actual native resolution the initial
effect bypasses both EASU and RCAS. Turning the effect off is a separate action.
On an output change, recompute the desired pixels from the stored percentage
or preset; never change the monitor mode or desktop scale to satisfy the wish.

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
| Proton/Xwayland game | Resolution selection and delivery depend on the game and Xwayland integration. The Wayland hint is not a generic control for Windows game render settings. | Verify separately on a real game; otherwise show that the resolution must be selected in the game. |
| Per-game display information | Advertising a smaller fullscreen resolution might cause the game to select a smaller buffer. | Investigate without KWin patches; keep mode information, fullscreen geometry, scale and input consistent. Not implemented in the product; proxy experiments below verify limited client paths. |
| Virtual output | KWin has backend APIs for creating an additional output. This does not itself give one game a private display environment. | Investigate only if simpler per-game control is insufficient; preserve physical-output HDR and VRR. Not part of the initial rendering path. |
| Nested compositor | A separate environment can advertise chosen screen modes, as gamescope does. | Consider a launch helper only when it forwards original smaller buffers into the existing KWin session; an already enlarged intermediate does not feed this effect. |

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

The desired percentage remains usable as a clearly labelled recommendation
when no verified request mechanism is available. Explain briefly: "Select
2560 × 1440 in the game", for example. If a supported request is sent, show it
as pending until a committed buffer confirms the result. Never label a saved
preference as applied merely because the configuration was accepted. If the
client ignores or adjusts it, show the actual dimensions and continue scaling
eligible buffers at their actual size. Do not repeatedly resend an ignored
request or repaint continuously while waiting. Apply a request on slider
release or explicit Apply, rather than at every drag position.

### Selecting the resolution control method

Required extension, not yet implemented: provide **Resolution control method**
in global settings, defaulting to **Auto**, with a sparse per-application
profile override. **Use global** inherits the current global method; an
explicit **Auto** override keeps automatic selection for that application even
if the global method later changes. Game detection selects the profile; it
does not by itself establish that resolution control succeeded.

| Method | Intended behaviour |
| --- | --- |
| Auto | Choose a verified compatible method for the selected application, runtime and available helpers. Prefer an applicable in-session negotiation before requiring a launch helper. |
| Wayland negotiation | Request a smaller buffer from a cooperative native Wayland client. |
| Display proxy | Launch the application through a private Wayland display, with private Xwayland where needed. |
| Gamescope | Launch through the verified Wayland buffer-forwarding backend; requires effect support for its surface tree. |
| Game settings only | Make no automatic resolution changes; show the desired pixels as guidance and scale eligible supplied buffers. |

Only implemented and verified methods may be enabled for the current case.
Show unavailable methods with a reason. An explicit method must not silently
switch to another method when it fails. Auto may use a verified alternative,
but must report the effective method and whether the target was reached.
Method selection is independent of the desired resolution, EASU and RCAS.
The existing **Automatic (use the supplied buffer)** resolution setting still
makes no resolution request, even when the control method is Auto. Choosing
**Game settings only** does not disable upscaling.

For launch-time methods, resolve the profile before starting the application
and preserve its association with the resulting window and child processes.
Detecting an already running game can select or create its profile, but cannot
retroactively place its connection behind a helper. Show **Restart required**
when a method change needs a new launch; never restart a running game or cycle
through helpers automatically. Status must distinguish the configured method,
effective method, pending launch and observed supplied-buffer resolution.

### Selecting the game

Resolution changes need explicit per-game selection. The proposed interface
lets the user select the active game window and optionally remember a profile
with its desired resolution. Match native Wayland windows by their application
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
Treat this as an unresolved minimum-version compatibility issue. These findings
concern test clients, not acceptance of real games or the production effect.

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

A launch helper plus the effect is a viable direction without modifying KWin.
Two mechanisms have supplied smaller original buffers to unmodified KWin 6.3.6:

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

The candidate launch shape for Gamescope is:

```sh
gamescope --backend wayland -w 1920 -h 1080 -W 3840 -H 2160 \
  -F linear -f -- command
```

Add `--expose-wayland` for native Wayland clients. These flags were exercised
with Gamescope 3.16.22 from Debian Trixie backports; they are experimental
integration guidance, not a claim that the current effect accepts this path.
The helper must start before display enumeration. A private display must be
associated with the selected game profile, including child processes; the
host-facing wrapper identity alone is insufficient to distinguish games.

Do not promise an exact rendering resolution for every program. A deliberate
fixed-4K client still submitted 4K through the smaller virtual display, and
internal render targets remain application-owned. Rejecting such buffers could
prevent presentation but would not make the game render less. Offer automatic
negotiation where verified, launch-time virtualization where supported, and
in-game guidance otherwise. Show **target not reached** when observation does
not confirm the requested size; continue scaling eligible actual input.

The measured results establish mechanisms, not product acceptance. Complete
per-game launch/profile integration, subsurface handling where needed, relative
input and confinement, exclusive fullscreen and mode transitions, overlays,
explicit synchronization, HDR and VRR, and real-game TV acceptance before
shipping. The experimental commands do not measure performance savings.

## Rendering and lifecycle requirements

EASU replaces the enlargement step and must receive the original buffer, not
an image already scaled to the destination. The initial geometry permits one
eligible fullscreen window on one output, with the full buffer visible and
no buffer transform. Multiple simultaneous candidates use normal KWin
rendering. Eligibility uses physical pixel sizes and capabilities rather than
fixed resolutions or GPU vendor checks.

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

## Validation requirements

Validate original-buffer pixel mapping and lifecycle behaviour against KWin's
virtual backend, and image quality, HDR and VRR on the real output with a real
game. Compare ordinary KWin scaling, EASU, and EASU with RCAS using identical
input. Measure the whole rendering path, including the cost of losing direct
scanout, rather than timing the shader alone.

Record actual results and outstanding checks in the corresponding slice
document. An SDR prototype, a documentation check or a configured VRR setting
does not establish completion of the required HDR and VRR support.

### Test applications and progression

Establish a reproducible baseline with small, open-source applications before
testing games from Steam or Epic Games Store. Use this initial application set
in order; the list is a test plan, not a record of passing runs:

| Stage | Application | Purpose and required selection |
| --- | --- | --- |
| 1: OpenGL benchmark | [glmark2](https://github.com/glmark2/glmark2) | Measure performance differences between native rendering, ordinary scaling, EASU and EASU with RCAS using repeatable fullscreen scenes. Test Wayland and X11 builds separately, with X11 through Xwayland. |
| 2: Vulkan benchmark | [vkmark](https://github.com/vkmark/vkmark) | Measure the same performance comparisons for Vulkan. Explicitly select Wayland and XCB in separate runs and keep presentation mode consistent within each comparison. |
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

The current implementation checks fullscreen rendering eligibility and shows
resolution guidance. It does not yet implement explicit game selection,
remembered game profiles or active client resolution control. These game tests
are therefore acceptance requirements for work still to implement, not features
established by loading the plugin or by the native-client experiments.

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
setting or restart, expose and test that workflow explicitly instead of claiming
an immediate change.

Record each game's actual window-system backend. Validate native Wayland and
Xwayland separately on the minimum supported KWin; successful control of the
cooperative test client does not establish control of these games. Once these
open-source game cases and the benchmark stages pass, repeat identification,
reduction and restoration with the selected Steam/Epic games under Valve Proton
and standalone Wine. An unavailable control path remains an implementation or
integration gap in this acceptance stage.

### Benchmark performance comparisons

The primary purpose of glmark2 and vkmark is to measure and assess performance
differences. Their functional checks establish that each measurement exercised
the intended path. Determine both the net benefit of lower-resolution rendering
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
