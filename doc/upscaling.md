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
- Game matching, launch/resolution handling and upscaling cooperate as one
  experience. Supported games should not require users to compose launch
  commands or discover a sequence of unrelated workarounds. Unsupported cases
  must be identified clearly, without pretending that a requested setting took
  effect.

The existing requirements for application profiles, resolution control, managed
launching, settings and validation specify the pieces of this experience.
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

Implementation and acceptance are tracked in the next
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
application data. Launch diagnostics report the selected method and outcome;
they do not expose arbitrary command-line arguments or environment values.
The diagnostic path must remain bounded and must not block rendering or query
the GPU synchronously.

### Settings page

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
| Mode | Upscale only, until a mode from the processing modes section above is implemented. Offer a selector only for modes that are implemented, and show why an unavailable mode does not apply to the current buffer. |
| Preferred game resolution | Automatic (use the supplied buffer), or a percentage slider with a numeric percentage and live width by height in physical pixels. |
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

Required extension, not yet implemented: maintain a user-editable list of
application profiles with create, inspect, edit and delete operations. Each
profile contains application matching information and only explicitly selected
setting overrides. Every absent setting follows the current global value;
changing a global setting must update all profiles that inherit it. False and
zero are valid overrides. An explicit value equal to today's global value stays
an override until the user selects **Use global**.

Identify windows through KWin's application ID/window class and optional instance,
using its interactive window detection service when adding a running application.
Do not use process scanning or a changing window title as the primary identity.
Known-application recommendations must remain editable and removable, and must
not overwrite user changes on update. Profiles do not bypass rendering
eligibility or automatically implement client-resolution negotiation.

The proposed model, code reuse findings, catalogue policy and required checks
are in the [application profiles slice](agents/slice-application-profiles.md).

### Game detection OSD

The planned on-screen display (OSD) must optionally announce when a game is
recognized by an application profile. Provide a **Show game detection** switch
and a configurable display timeout in seconds. Both settings follow the same
global-default and sparse per-application override model as other settings.

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

Use the planned OSD rather than introducing a second notification surface. Draw
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
when the display is hidden. It announces the *selected* application and shows the basic summary for
the configured timeout. Nothing recognizes games yet, so no profile match or
game identity is claimed. The announcement is keyed to the selected window:
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
| A helper, resolution method or launch parameter that requires a new process | Save for the next launch and show **Restart required**, while retaining the current effective settings for the running game. |

Mixed changes apply their live portion immediately and keep only the remaining
portion pending. Reverting a pending value to the effective value clears that
pending change. Saving or applying settings must never restart a game on its
own. Offer **Restart game and apply** alongside **Apply on next launch**, using
the [restart workflow](#restarting-a-game-with-pending-settings) below.
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
current. Desired resolution, black bars, resolution method, pending restart and
filter timing belong to features that do not exist yet and are not shown.

Use event-driven samples and bounded text updates, with no synchronous GPU
readback or continuous full-screen repaint loop just to animate statistics.
Hiding the view stops its sampling overhead and releases its resources and
any composition requirement. Draw text at output resolution after the game
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
above. Profile and match origin, pending values, drawn image and bars, capture
and intermediate formats, observed VRR state and measured filter timing name
features that are not implemented; they are absent rather than filled with
plausible values, and the colour line says that VRR is not observed.

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
do not implement future launching, profiles or rendering modes just to fill
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

Automatic is the default and makes no resolution request. Selecting Native
requests or recommends native input; processing still follows the actual
buffer until the client changes it. At actual native resolution the initial
effect bypasses both EASU and RCAS; the proposed sharpen-only mode would be
the explicit exception, and it does not change this default. Turning the
effect off is a separate action.
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
when a method change needs a new launch. Ordinary launches must not restart a
running game or cycle through helpers; an explicit discovery session follows
the controlled trial workflow below. Status must distinguish the configured method,
effective method, pending launch and observed supplied-buffer resolution.

### Application launch configuration and method discovery

Required extension, not yet implemented: each application profile can contain
an optional launch configuration and a **Find best method** action. Users can
configure a new application without first running it, or add launch information
to a detected application's profile. Saving or detecting a profile does not
launch anything. Applications started elsewhere can still match their profiles,
but launch-time control requires their launch path to use the helper.

| Launch field | Requirement |
| --- | --- |
| Program | Executable or an explicitly selected launcher, with a file picker and validation. |
| Arguments | Preserve argument boundaries, empty arguments, spaces and Unicode. Offer an editable argument list and a readable command preview. |
| Working directory | Optional explicit directory; show the resolved default. |
| Environment | Inherit the session environment with per-profile additions, replacements and explicit removals. Scope changes to the launched application and helpers. |
| Runtime | Native, Wine, Proton or an external launcher, with the applicable runtime path/version, Wine prefix or compatibility-data location, game identifier and launcher options. |
| Advanced launch | Support an explicitly selected shell command or user script for launches that cannot be expressed as a program and arguments. Ordinary launches do not implicitly interpret shell operators or expand variables. |

Keep launch configuration separate from window-matching identities and scaler
setting overrides. Use structured process arguments and environment values,
with Qt process APIs in the helper; do not execute game commands inside KWin.
Show which helper and runtime will wrap the actual game command. Validate
missing programs, directories, runtime components and incompatible options
before attempting a launch, and report actionable errors. Do not include the
full environment or sensitive argument values in routine diagnostics.

An external launcher may hand the request to an already running process.
Starting Steam or another launcher with modified environment variables does
not prove that its eventual game inherits them. A launcher adapter must arrange
wrapping at the actual game launch and correlate the resulting window with the
profile, including child processes. If this is not supported, report it and
provide launch-integration guidance instead of claiming that the launcher
itself is the controlled game. Preserve the configured runtime and prefix;
do not silently substitute another Wine/Proton version to make a test pass.

**Find best method** starts an explicit, cancellable discovery session for the
profile and selected target resolution:

1. Check available helpers and runtime capabilities; exclude inapplicable or
   unimplemented methods and explain why. If the target is **Automatic (use
   the supplied buffer)**, require a concrete target for the experiment without
   silently changing the saved preference.
2. Try supported candidates in a documented order, preferring applicable
   in-session negotiation before a launch helper. Each trial uses the saved
   command, arguments, directory and environment with that candidate's wrapper.
   Explain that discovery can start and close multiple test instances.
3. Associate the actual game window with the trial. Observe its supplied buffer,
   fullscreen destination and stable presentation; a process starting, a saved
   mode or a smaller image produced by downsampling is not success. Record
   adjusted sizes separately from an exact target match.
4. Exercise available automatic input/lifecycle checks and record user-observed
   checks separately. If pointer behaviour, image quality, HDR or VRR cannot be
   established automatically, mark them unverified. Detection, rendering and
   full compatibility are separate results.
5. Close the trial gracefully and clean up owned helpers before a method that
   needs a new launch. Bound startup, observation and shutdown waits. If the
   application does not exit, stop the sequence and report it; do not force-kill
   it or affect unrelated application or launcher instances. Cancellation stops
   further trials and restores any temporary control policy.
6. Recommend the best verified compatible candidate for the requested features.
   Prefer an exact target match, correct presentation/input and clean lifecycle;
   use the documented method order to break otherwise equal results. A fastest
   method requires comparable performance measurements; startup success or
   resolution alone cannot establish it. If none passes, retain the failures
   and offer game-setting guidance without marking the target applied.

Store discovery results separately from the user's selected method. **Auto**
can reuse a compatible verified result; discovery must not overwrite an
explicit method override. Record the tested launch configuration, runtime and
helper versions, compositor/backend, target size, output scale and feature
conditions such as HDR/VRR. Relevant changes invalidate the cached recommendation
or require a new check. Continue observing actual buffers on ordinary launches;
a cached success is not proof that today's launch reached its target.

Ordinary **Launch** uses the selected explicit method or Auto's current compatible
recommendation. It does not start an unattended trial cycle in an active game.
When discovery or a new launch is needed, show that state and provide the
corresponding action. Present each trial's method, observed dimensions, checks,
failure reason and remaining uncertainties, with **Retest** and **Clear results**.
Keep these results distinct from the user's launch configuration and profile
settings so clearing results does not delete either.

### Restarting a game with pending settings

Required extension, not yet implemented: **Restart game and apply** reuses the
launch definition for the current game, including the executable or launcher,
exact argument list, working directory, environment, runtime/version, prefix
or compatibility-data location and game identifier. Preserve a record of the
resolved launch used for helper-managed instances, including inherited
application environment values and explicit removals. Replace only the
settings the user changed and regenerate helper-owned connection values for
the new instance; do not reuse a private display socket that was destroyed.
Keep environment values in memory for the running launch, out of routine logs;
save only the configured overrides in the profile.

Present the pending changes and make clear that restarting closes the game and
may lose unsaved progress. The explicit restart action begins one controlled
close-and-relaunch operation; an ordinary Apply or profile match cannot trigger
it. Validate the new launch before closing the current instance. Request a
graceful close, allow the game's save/exit dialog to complete and wait for
confirmed termination before cleaning up owned helpers and starting one
replacement. If closing is refused or times out, leave the game running,
retain pending settings and report the outcome; do not force-kill or start a
duplicate. Cancellation stops the next launch and must not report success.

Use the existing launcher integration to identify and restart the actual game,
including child processes. Do not close Steam or an unrelated game to restart
one title. A window match alone cannot reconstruct its command and environment:
for an externally started game without a verified launch record or adapter,
show **Launch setup required** and allow the user to supply its launch
definition. Do not claim an identical relaunch from guessed arguments or a
launcher process's environment.

Keep failures actionable and preserve the launch definition and pending
changes for retry. After relaunch, associate the new window with the same
profile and verify the actual method and supplied buffer before marking the
requested change effective. Restart is not a method-discovery session and
must not cycle through alternative helpers.

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

### Launching through a launcher's own options

Proposed route, not decided and not implemented. A launcher that already wraps
every game command is a cheaper path to launch-time control than reproducing
the launcher inside the configuration module. Steam applies a per-game launch
option template to the actual game process, so a wrapper named there sits
between Steam and the game:

```sh
kwin-upscale-run -- %command%
```

Lutris, Heroic and Bottles offer an equivalent command-prefix field, and the
same executable can be used by hand from a terminal. This addresses the
limitation recorded above: starting Steam itself with a modified environment
does not establish what its eventual game inherits, while a wrapper in that
game's own launch options runs in the game's process ancestry. The wrapper,
not KWin, would start a display proxy or gamescope where a method needs one.

- The wrapper ships as a separate executable in the package and runs without a
  running configuration module. KWin must not execute game commands.
- It correlates its launch with a profile explicitly, by an identifier it
  passes to the helper environment, so the effect can associate the resulting
  window and any child processes. A wrapper identity alone does not identify a
  game.
- Argument boundaries, empty arguments, spaces and Unicode survive unchanged,
  including the launcher's own quoting of `%command%`. Ordinary launches do not
  interpret shell operators.
- It exits with the wrapped command's status and forwards signals. A game the
  launcher can no longer stop is worse than no wrapper at all.
- With no verified method available for that profile, it runs the command
  unchanged rather than failing the launch, and says so.
- The profile editor shows the exact line to paste, where to paste it, and
  that the launcher applies it only to the next launch. Generating copyable
  text is the feature; the paste stays with the user, and a game started
  without the wrapper cannot be adopted by it afterwards.

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

The [pipeline slice](agents/slice-build-release-pipeline.md) records validation and
remaining hosted, BSD and hardware acceptance work.
