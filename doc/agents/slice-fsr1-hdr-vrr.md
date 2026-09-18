<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: FSR rendering with HDR and VRR

## Start state

FSR 1, optional RCAS and global resolution controls are implemented. The dated
shader/configuration builds and tests below passed in their recorded copies;
they are not a new verification of concurrent source changes. Integration,
real-game performance, HDR/VRR and joint TV acceptance remain open.

## End state

The supplied-buffer rendering path passes supported-container, lifecycle,
image-quality, performance and real-device acceptance for the required SDR,
HDR and VRR combinations. Native-size/unsupported cases restore normal KWin
rendering, and measured limitations and design invariants are preserved in
source and the handbook. All required acceptance below has passed.

## Supported scope and full acceptance

The [handbook](../upscaling.md#supported-scope-and-full-acceptance) defines both
gates. For this package they are:

**Supported scope, releasable.** An eligible opaque fullscreen buffer between
half and full destination size, matching aspect ratio, unrotated output, SDR,
on KWin 6.3.6, is scaled with EASU and optional RCAS; every other case falls
back to ordinary KWin rendering and the status names the reason. Closing this
gate requires the real-session eligibility defect to be fixed and one scaled
frame observed on hardware, the lifecycle and fallback checks, and the A0/A1
overhead bounded with phase-reversed repeats. It does not require a game,
Proton, HDR, VRR or the B-to-D cost matrix.

**Full acceptance.** The required acceptance matrix below, unchanged. HDR, VRR,
real games under Valve Proton and standalone Wine, image quality and the
complete cost matrix keep this requirement open after any release.

VRR is currently blocked rather than pending on the only available acceptance
host, whose output reports adaptive sync incapable. Record it as blocked and
obtain a second output path; do not count it as passed or drop it.

## The scaler-effective gate

This gate is the package's first milestone and the project's next acceptance
after the [development infrastructure slice](slice-development-infrastructure.md).
It exists because the effect has never been observed scaling a frame. Loading
the plugin, reporting it supported, passing the container and headless tests and
installing it on the acceptance host have all been achieved and none of them
establish that the scaler does anything.

The gate closes when the effect demonstrably processes a supplied smaller buffer
on real hardware, and not before:

| Required observation | Why it is not implied by the current evidence |
| --- | --- |
| A named eligibility condition explains every refusal | The real session refuses a conforming buffer and reports only the generic rule text. The refusing condition is unknown. |
| The refusal recorded on 2026-09-18 is understood and fixed | Desktop scale and buffer size are ruled out; the cause is not isolated and no fix is claimed. |
| `activeEffects` lists `upscale` for an eligible window | The effect never entered KWin's active set, so it never took its scanout restriction. |
| The destination pixels differ from ordinary KWin scaling | A covered output proves composition happened, not that EASU ran. |
| An ineligible window still restores ordinary rendering | Fallback has only been exercised where the effect was already inactive. |
| An automated test reproduces the real-backend refusal | The container and headless suites pass against the same case that fails on hardware. That is a coverage gap, not only a defect. |

The last row is the durable part. A fix that only repairs the running session
leaves the suites unable to detect the next occurrence, so this gate requires
the automated coverage to be extended to whatever distinguishes the real
graphics backend from the existing fixtures.

The gate needs no game, no Proton, no HDR, no VRR and no resolution control. The
fixture built from `autotests/wayland_client.cpp` already supplies a conforming
buffer on request. Nothing here waits for the joint acceptance session.

Depends on the candidate selection and rejection reporting from the development
infrastructure package. Diagnosing this refusal is the reason that package is
sequenced first.

### The refusing condition, found on 2026-09-18

The condition is `TransformedRenderTarget`: the effect refused every frame
because the render target was not upright. It was found by running the
production plugin against SuperTuxKart in a nested KWin 6.3.6, where the
buffer was eligible and the status named this condition for the frame.

It is not specific to that session. KWin's DRM backend begins every frame with
`m_pipeline->output()->transform().combine(OutputTransform::FlipY)`
(`src/backends/drm/drm_egl_layer.cpp`), so on an upright screen the target
transform is `FlipY` on **every** composed frame of a real session. The check
therefore refused everything on real hardware, which is what "the effect has
never been observed scaling a frame" was recording.

The check was also unnecessary. `RenderViewport`'s projection matrix already
contains `renderTarget.transform().toMatrix()`
(`src/core/renderviewport.cpp`), and the scaler draws through that matrix, as
KWin's own effects do. Nothing had to be compensated for.

Fixed by accepting an upright or flipped target and refusing only orientations
nothing has drawn through; a rotated output is still refused earlier by its own
condition. A render test draws the same image into an upright and a flipped
target and requires the results to be mirror images, and it fails when the
expectation is inverted, so it is not vacuous.

Observed afterwards, with the production plugin, factory defaults, a
3840 × 2160 output at desktop scale 3 and SuperTuxKart configured for
fullscreen at 3840 × 2160: supplied input 2560 × 1440, destination
3840 × 2160, processing **FSR 1, sharpening 0%**. This is the first observation
of the scaler processing a real game's buffer. It is a headless nested session
with a real GPU: it establishes that the path runs and is not image-quality,
performance, HDR, VRR or television acceptance, and the gate's remaining
requirements stand.

### Driving the benchmarks, and why the first numbers are not results

Jens asked for glmark2 and vkmark fullscreen at half resolution with the effect
enlarging, measured against the same run at 3840 × 2160. Both were read in
source and then run; both now work through the plugin, and the measurement
environment does not.

#### What each benchmark actually reads

Neither benchmark behaves like SuperTuxKart, and neither could have been driven
by guessing.

- glmark2 2023.01, `src/native-state-wayland.cpp`: on a granted fullscreen
  configure it sets its size to the configure size multiplied by the output
  scale and calls `wl_surface_set_buffer_scale` with that scale. It reads the
  advertised mode only in the branch where fullscreen was refused, which is why
  an advertised mode alone changed nothing: measured, 3840 × 2160 unchanged.
- vkmark 2025.01, its Wayland window-system source `wayland_native_system.cpp`: its fullscreen extent is
  the advertised mode in pixels, and it separately calls
  `wl_surface_set_buffer_scale` with the advertised scale. An advertised mode
  alone therefore shrank its window to 1920 × 1080 on a 3840 × 2160 screen, and
  an advertised scale alone stretched its surface to twice the screen.
- SuperTuxKart 1.4, `lib/graphics_engine/src/ge_vulkan_driver.cpp`: its Vulkan
  swapchain is `SDL_Vulkan_GetDrawableSize`, which is why the Vulkan renderer
  follows the window rather than a mode and the advertised mode does not reach
  it. Its OpenGL renderer goes through SDL's mode emulation and does.

#### Observed with the production plugin

A 3840 × 2160 virtual output at desktop scale 2, the effect at its Performance
preset, each benchmark started by the session rather than by the effect:

| Client | Committed buffer | Destination | Effect |
| --- | --- | --- | --- |
| glmark2, `--fullscreen -b terrain` | 1920 × 1080 | 3840 × 2160 | FSR 1, sharpening 0% |
| vkmark, `--fullscreen -b cube` | 1920 × 1080 | 3840 × 2160 | eligible |

Exactly half, fullscreen, covering the output, through the effect's own
catalogue and method selection.

#### The numbers from this environment are unusable

glmark2's terrain scene scored 40 at 3840 × 2160 and 42 at 1920 × 1080 **with
the effect disabled in both runs**: a quarter of the pixels bought five per
cent. The client is therefore not what limits this environment, so no
comparison made in it can measure what the effect costs or saves. The run with
the effect enabled scored 40 against the 4K baseline's 39, which says nothing
for the same reason.

Two causes are known and neither is the effect: a nested compositor composites
every frame at the full output size, and a Wayland client is paced by frame
callbacks. Do not record any of these figures as a performance result.

The performance matrix therefore stays where the handbook already puts it: the
real session on the acceptance host, with the physical output, its own
presentation timing and direct scanout in the picture. The one thing these runs
establish is that the matrix can now be driven at all, which it could not be
before: A0 and C differ only in what the client was told.

#### What the benchmarks turned out to be good for, 2026-09-18

The benchmarks were adopted as the instrument for measuring what reducing the
rendering resolution is worth. Read in their own source, glmark2 cannot do
that: its `terrain` scene runs its height, normal, specular and both bloom
passes at fixed 256 × 256 and 512 × 512 resolutions and draws a hundred and
thirty thousand triangle grid, so almost nothing it does follows the window.
Measured windowed, with this effect uninvolved, a quarter of the pixels made it
*slower*: 21 frames per second at 1920 × 1080 against 26 at 3840 × 2160. Every
comparison run through it, including the ones recorded earlier in this
document, was therefore measuring an instrument that cannot move.

vkmark's `effect2d` does follow the window: its render area is the swapchain
extent and its kernel steps are one over that extent. It went from 20.8 ms a
frame at 1920 × 1080 to 27.0 ms at 3840 × 2160, so roughly six milliseconds a
frame is what rendering 4K instead of 1080p costs that workload here. That is
the size of the prize this effect is competing for.

Jens's conclusion, and the one recorded here: the benchmarks stay, for
compatibility and for catching a regression, rather than as the measure of a
gain. They cover ground the two games do not. glmark2 is scale-driven and
vkmark is mode-and-scale-driven, so between them both remaining control methods
are exercised. vkmark commits `DRM_FORMAT_XBGR16161616`, which is how the
scaler's missing 16-bit formats were found: every frame of it was refused, and
nothing else in the test set had shown that. With presentation now measured
from `RenderLoop::framePresented`, "the effect did not make this worse" is
checkable for a client it cannot help.

Anything used to measure a gain has to be shown to respond to resolution first,
windowed and with the effect uninvolved. That check costs two runs and would
have saved every measurement made on 2026-09-18.

#### What a real run needs

- The 4K output at **desktop scale 2** for an exactly half-resolution
  comparison. At scale 3 the reachable step is two thirds, and at scale 1 these
  two clients cannot be reduced at all.
- A scene heavy enough to sit below the refresh rate at 3840 × 2160, or the cap
  hides the difference. vkmark's `-p immediate` for the throughput series.
- Both series from the handbook, and composition versus direct scanout recorded
  for each case: enabling the effect gives up scanout, and that cost belongs in
  the end-to-end number rather than being left out of it.
- SuperTuxKart's own `--profile-time` as the sanity check before the matrix. It
  works at any desktop scale, because its method does not go through the
  integer scale.

## Scope and boundaries

Own original-buffer capture, EASU/RCAS, colour handling, initial geometry,
rendering lifecycle/fallback, global controls and the rendering acceptance
matrix. Profiles, overlays, launching and active resolution control each have
separate packages. Extended aspect ratios and integer filters belong to
[geometry](slice-scaling-geometry.md). Optional later features do not gate this
slice's completion.

## Dependencies

Rendering tests can use controlled original buffers and manual game resolution
settings. Where required real games need a helper to supply such buffers, use
the verified [resolution-control method](slice-resolution-control.md) and record
that dependency. Its method development and game-control success criteria are
owned there, not duplicated here. Keep the handbook's staged application order;
a dependency still needed for required rendering acceptance keeps this slice open.

## Approach

1. Retain the source-resolution capture and reversible colour path, verifying
   actual original-buffer mapping through KWin's renderer on both targets.
2. Complete compositor lifecycle/fallback and global configuration checks.
3. Run the staged OpenGL/Vulkan performance comparisons, then real-game image,
   HDR and adaptive-presentation acceptance with Jens on the TV.
4. Fix rendering failures, record observed limits and preserve implementation
   explanations in source before deleting this document after acceptance.

## Rendering design

The handbook's [selected initial approach](../upscaling.md#selected-initial-approach)
specifies the scaler choice, the supported initial geometry, the colour path and
the acceptance this package has to reach. It is the single source; this document
does not restate it.

Within that scope this package owns the global controls: the percentage slider
with its pixel preview, the FSR resolution presets, automatic mode, the optional
RCAS strength and the separate desired and actual resolution status. Resolution
wishes stay guidance here; verified client requests belong to the
[resolution-control package](slice-resolution-control.md). Scaling always follows
the buffer that actually arrived, including games with a fixed size, and a
mismatch with the wish alone must not disable it. The wish is never implemented
by downscaling a completed frame. HDR and VRR remain mandatory throughout.

## Findings

- KWin 6.3.6's `OffscreenEffect` captures at output scale into `GL_RGBA8`
  with an sRGB description. It cannot supply the required input unchanged.
  The implementation renders the surface item at buffer resolution
  through KWin's item renderer, retaining its import and release-fence handling.
- Colour conversion to the current render target happens at input
  resolution. Floating-point intermediates and a reversible working encoding
  are required before EASU/RCAS; final composition must not convert colours a
  second time. This encoding still requires HDR image-quality acceptance.
- KWin 6.3.6 selects adaptive presentation in `compositor_wayland.cpp`
  independently of direct scanout. This establishes an integration path, not
  proof of VRR on a physical display.
- The implementation now contains the source-resolution capture, EASU and
  optional RCAS, eligibility checks, normal-rendering fallback and settings.
  Container shader tests are separate from full compositor and TV acceptance.
- Gamescope's reviewed FSR path scales the base layer with EASU and combines
  RCAS with final composition. Separately composited overlays follow the base
  layer; an in-game HUD is already part of the input buffer.
- Gamescope's reviewed SGSR path uses a modified filter followed by RCAS. It
  cannot be treated as Qualcomm's single-pass reference when comparing costs.
- In the reviewed gamescope code, `update_tmp_images()` allocates an 8-bit
  intermediate image, and the NIS wrapper leaves its HDR modes at their SDR
  defaults. These are not a ready-made HDR integration for KWin.
- FSR's input range and transfer-function assumptions require an explicit HDR
  conversion design. Raw scRGB must not simply be clamped into an SDR buffer.
  Using FP32 shader arithmetic does not by itself solve intermediate-format
  precision or colour management.
- FSR documents a useful range up to four times the input pixel count. The
  1080p/1440p to 4K cases fit that range; 720p to 4K is outside this slice.
- NIS has explicit linear-HDR and PQ modes and remains an alternative if the
  FSR integration cannot meet the required HDR behaviour. HDR and VRR remain
  requirements if the scaler choice changes.

These are source-review findings, not measured quality, timing or power
results for this effect. [Gamescope's reviewed renderer](https://github.com/ValveSoftware/gamescope/blob/c50ddfa9b71a75ec8df94bda8cf31d425dbdda24/src/rendervulkan.cpp),
[AMD's reference](https://github.com/GPUOpen-Effects/FidelityFX-FSR) and
[NVIDIA's reference](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
document the reviewed pipelines and algorithm inputs. Other sources remain
listed in the [README](../../README.md).

Resolution-method source findings and prototypes are owned by the
[resolution-control slice](slice-resolution-control.md); the current production
effect sends no client requests.

## Progress and remaining work

- The README now leads with the observed non-working state and a short overview;
  build and package availability are explicitly not claims of working upscaling.
- [x] Select and implement FSR 1, optional RCAS and global configuration.
- [x] Implement colour conversions and record automated shader/configuration tests.
- [x] Measure A0 and A1 on the real output; record the unusable aggregate score.
- [ ] Close the scaler-effective gate above: isolate and fix the refusal, observe
  a scaled frame on hardware, and extend automated coverage to reach the case.
- [ ] Complete original-buffer and lifecycle integration acceptance.
- [ ] Measure runs B, C and D; blocked on resolution control on KWin 6.3.6.
- [ ] Complete real-game, HDR/VRR, image-quality and TV acceptance. VRR is
  blocked on this host: HDMI-A-1 reports adaptive sync incapable.

## Findings from validation

### Implementation checks, 2026-09-18

- The production resolution policy passed its compiled regression test:
  exact presets, nearest-integer rounding, percentage bounds, aspect tolerance,
  native-size bypass, unsupported sizes, large integer dimensions and RCAS
  bypass/strength. It runs through the existing tooling-tests hook.
- Mesa llvmpipe executed the actual shader resources through KWin's EGL,
  shader, framebuffer and texture classes on Trixie. Desktop OpenGL and
  OpenGL ES passed data-driven constant-colour tests for sRGB, gamma 2.2,
  linear and PQ, including negative linear values and HDR range. Orientation,
  resizing, sharpening, clipping, scissor-state restoration, rejection of
  oversized intermediate textures and settings tests also passed.
- Runtime testing found and fixed KWin 6.3.6's `_core` shader-resource lookup,
  its GLES allocator silently using 8-bit storage and the default GLES sampler
  precision discarding HDR detail. These are covered by the rendering tests.
- The configuration tests exercise Automatic, exact preset labels and pixel
  previews, keyboard percentage changes, slider bounds, RCAS defaults and
  save/load/defaults against an isolated configuration directory.
- A 3840 × 2160 virtual KWin 6.3.6 session accepted a native Wayland client's
  1920 × 1080 buffer with a fullscreen viewport. No DRM render node is available
  in this environment, so that KWin uses QPainter and correctly refuses the
  OpenGL-only effect. A nested Wayland KWin also requires a DRM device for its
  OpenGL backend. Neither run establishes compositor shader or VRR acceptance.
- A separate headless test exercised KWin 6.3.6's actual item renderer with a
  synthetic surface. Source capture, EASU and EASU with RCAS preserved the test
  image's orientation and constant colour patches. Capturing a 32 × 18 source
  containing a one-pixel pattern and scaling to 64 × 36 produced exactly the
  same 8-bit readback as scaling the original uploaded image directly (maximum
  channel difference zero), both with and without RCAS. This test used the locally
  checked out 6.3.6 source headers because Debian's development package omits
  an internal header needed by `ItemRendererOpenGL`.

Final automated results on amd64, 2026-09-18:

| Environment | Build | Tests |
| --- | --- | --- |
| Trixie, KWin 6.3.6, GCC | Passed, warnings as errors | 4/4 CTest entries passed, including desktop GL, GLES, configuration and AppStream |
| Trixie, KWin 6.3.6, Clang | Passed, warnings as errors | 4/4 CTest entries passed |
| Neon unstable, KWin development snapshot, GCC | Passed, warnings as errors | 3/3 CTest entries passed, including GLES, configuration and AppStream |
| Neon unstable, KWin development snapshot, Clang | Passed, warnings as errors | 3/3 CTest entries passed |

The Neon snapshot was package version
`4:6.7.5+p24.04+vunstable+git20260915.1132-0`. Trixie's initial GLES test run
could not start because the existing build image lacked `libGLESv2.so.2`.
Installing the declared `libgles2` test dependency in the runtime container
resolved this; both compiler builds then passed there. No test was skipped.

`pre-commit run --all-files` and the complete `pre-push` stage passed in the
Trixie container on a working-tree copy under `build/`, including the compiled
resolution-policy regression, tooling tests and REUSE. The manual rendering
hook passed against the configured Trixie GCC build. `clang-tidy` passed for
the plugin sources, and the plugin metadata passed KDE's JSON schema check.
A Trixie staging install placed the effect and configuration modules in KWin's
respective `effects/plugins` and `effects/configs` plugin directories.

Native installation on wzpc, 2026-09-18: the Release build against installed
KWin 6.3.6 passed with warnings as errors, and all four native CTest entries
passed. The missing KCM development dependencies were installed from Trixie.
The effect and its configuration module were installed under `/usr`; SHA-256
checks confirmed that the installed files match the native build. An isolated,
offscreen KDE plugin-discovery probe found `Upscale` in the standard plugin
directory, confirmed its disabled-by-default metadata and instantiated the
installed configuration module successfully. No KWin instance was started or
effect enabled for this installation.

First session check, 2026-09-18: Jens subsequently logged into KDE, found the
settings entry and enabled the effect. In that native KWin session, the D-Bus
query `isEffectLoaded("upscale")` returned true. The effect's status was
`Inactive: no supplied window buffer.` This confirms discovery and loading;
no scaled game frame or image-quality acceptance was observed in this check.

The maintained rendering and settings tests are built under `autotests/` and
run with `pre-commit run upscale-render-tests --all-files --hook-stage manual`
after configuring and building. Set `UPSCALE_BUILD_DIR` when using a build
directory other than `build`. This hook also runs in the Trixie build CI jobs.
It does not replace the virtual-compositor lifecycle or real-device checks.

Open acceptance: full compositor lifecycle and original-buffer mapping with an
OpenGL virtual backend; real Wayland, Valve Proton and standalone Wine games;
Xwayland buffer compatibility; HDR gradients/gamut/transitions; VRR with composition; total
GPU time, frame times and power; joint TV image-quality comparison. Jens has
explicitly deferred the real-device tests to a later joint session.

### Code review follow-up, 2026-09-18

A review of the implemented slice produced the following changes. Each one is
accompanied by the source comment that records the reason next to the code.

- **Direct scanout is no longer blocked for a window the effect cannot draw.**
  Eligibility is decided from the window and its output, but the render
  target's colour description only becomes visible inside a paint pass. A
  candidate whose target this scaler cannot decode is now remembered, which
  takes the effect out of KWin's active set and releases the scanout
  restriction instead of forcing composition for a frame that falls back to
  KWin's own rendering. A different candidate or a reconfiguration clears it.
  This is covered by the integration test, which fails without the change.
- **The version and precision preamble of both shaders now comes from the
  loader.** KWin 6.3.6 hands a shader file to the compiler unchanged, so a
  file starting with `#version 140` reaches an OpenGL ES context with a
  directive that GLSL ES does not define; Mesa accepts it, other drivers need
  not. The preamble is now chosen in `scaler.cpp`, which also declares the
  float, sampler and integer precision that the OpenGL ES fragment language
  leaves unspecified or medium. Newer KWin supplies its own preamble and
  ignores this one.
- **RCAS operates on a full-range signal again.** Its noise limiter is written
  for values that fill 0 to 1, while the working encoding reserves its lower
  half for negative linear values, leaving ordinary content in 0.5 to 1. The
  sharpening pass now works in the `2 * value - 1` domain. The filter itself is
  affine-equivariant, so only the limiter's decision changes. Image quality
  still requires the joint acceptance session; this removes a known mismatch
  with the published filter rather than settling how sharp the result looks.
- **The destination-sized intermediate is released when sharpening is off**,
  and a texture whose framebuffer could not be completed is no longer kept,
  where it would have made every later attempt at that size fail.
- **Smaller changes.** The failure message uses a KWin logging category; the
  configuration module takes its defaults from `upscaleconfig.kcfg` rather
  than repeating them; damage widening tests the damaged window directly
  instead of searching the stacking order on every damage event of every
  window; the transfer-function numbers the shaders use are asserted against
  KWin's enumeration at compile time.

Two review findings were investigated and deliberately not acted on:

- `SurfaceItem::updatePixmap()` in the eligibility check looks like a side
  effect in a query path, but it is required. KWin creates the surface pixmap
  inside `ItemRenderer::renderItem`, which runs after the effect chain, so
  without it the effect is never eligible and never reaches the paint pass
  that would create the pixmap. Removing it made the integration test fail;
  the call now carries a comment saying so.
- `supported()` asking for OpenGL 3.0 on the development API and 3.1 on 6.3.6
  is correct, not an oversight: KWin dropped its desktop OpenGL backend along
  with that API, so 3.0 there means OpenGL ES 3.0 and supplies GLSL ES 3.00.

Observed results for these changes, all on amd64:

| Environment | Build | Tests |
| --- | --- | --- |
| Trixie, KWin 6.3.6, GCC | Passed, warnings as errors | 6/6 CTest entries passed |
| Trixie, KWin 6.3.6, Clang | Passed, warnings as errors | 6/6 CTest entries passed |
| Neon unstable, KWin development snapshot, GCC | Passed, warnings as errors | 4/4 CTest entries passed |
| Neon unstable, KWin development snapshot, Clang | Passed, warnings as errors | 4/4 CTest entries passed |

`clang-tidy` passed for every source under `src/`, and `pre-commit run
--all-files` passed in the Trixie container for both the commit and the
pre-push stage. `.clang-tidy` gained two exceptions for the function that
`Q_LOGGING_CATEGORY` defines, because KWin's own effects declare their
categories in exactly that way.

Still open from the review, and not addressed here: the intermediate images are
`RGBA32F`, which is about 160 MiB for a 1080p input on a 4K output. The bounded
working encoding would allow `RGBA16F` for the destination-sized image, whose
values stay within 0 to 1, while the input image keeps destination-encoded
values that can be large or negative. This belongs with the cost measurements
rather than ahead of them. The configuration module still derives its pixel
preview from `QScreen::devicePixelRatio()`, which Qt may quantise at
fractional desktop scales; the effect reports the true destination size over
D-Bus and the preview could use it.

### Planned application tests, 2026-09-18

Jens requested a staged application suite: fullscreen OpenGL and Vulkan
benchmarks, a simple Tux Racer family game and an open-source Vulkan game,
followed by games from Steam and Epic Games Store once the initial suite works.
The handbook now makes Valve Proton and standalone Wine mandatory and defines
the [test order and pass criteria](../upscaling.md#test-applications-and-progression).

Jens clarified that the benchmarks' primary purpose is to measure and assess
performance differences. The handbook's
[measurement protocol](../upscaling.md#benchmark-performance-comparisons) therefore
requires native-resolution baselines, native-size bypass, ordinary KWin
scaling, EASU, and EASU with RCAS. Lower-resolution cases cover 1080p and 1440p
on the unchanged 4K output. Uncapped throughput and cost at a fixed frame rate
are separate series, with warm-up, repeated runs, frame-time distributions and
recorded presentation/scanout state. No performance measurements or speedup
claims have been established yet.

The [resolution-control package](slice-resolution-control.md) owns the
open-source game identification/reduction checks in this shared test sequence. The handbook now specifies the
[game-control acceptance cases](../upscaling.md#game-identification-and-resolution-control-tests):
identify and select the correct game, obtain smaller committed buffers through
our control, leave unrelated windows and the physical output unchanged, and
restore normal policy. Manually choosing a smaller in-game resolution only
provides a baseline. Explicit game selection/profiles and active resolution
control remain unimplemented, so those acceptance cases cannot yet pass.

Selected applications and the package candidates observed with
`apt-cache policy` on wzpc:

| Order | Application | Trixie package candidates | Status |
| --- | --- | --- | --- |
| 1 | glmark2, native Wayland and Xwayland | `glmark2-wayland`, `glmark2-x11`: `2023.01+dfsg-2` | Planned; not installed or run |
| 2 | vkmark, Wayland and XCB | `vkmark`: `2025.01-1` | Planned; not installed or run |
| 3 | Extreme Tux Racer, OpenGL | `extremetuxracer`: `0.8.4-1` | Planned; not installed or run |
| 4 | SuperTuxKart, explicitly using Vulkan | `supertuxkart`: `1.4+dfsg-5+b1` | Planned; renderer availability and actual backend still to verify |
| 5 | Steam and Epic games with Valve Proton and standalone Wine | Choose titles and record runtime versions after the initial suite passes | Planned; Windows graphics, HDR and VRR coverage still open |

The application choices were checked against their upstream documentation;
package availability is not an execution result. In particular, the benchmarks'
fullscreen option selects the output size and does not establish a smaller
fullscreen buffer. Each backend needs an observed smaller-buffer path while
the physical output stays at 4K. The known Xwayland mismatch on KWin 6.3.6
remains open and must not be bypassed by testing only newer KWin or native Linux
clients. No additional test application was installed or launched for this plan.

### First runs on real hardware, 2026-09-18

Observed in the running KDE Wayland session on wzpc: Debian 13, KWin 6.3.6
(`4:6.3.6-1`), AMD Strix Halo with Mesa 26.1.2 and the `radeonsi` driver.
HDMI-A-1 presents 3840 x 2160 physical pixels at 120 Hz, normally at desktop
scale 3. The installed effect was loaded and reported supported throughout.
These are real-display runs; they are not the joint image-quality session,
which remains deferred.

The output reports `Adaptive Sync: incapable`. VRR acceptance cannot be
performed on this host and display link at all, so it is blocked rather than
merely untested. A second output path is required before VRR can be verified
here; the requirement itself is unchanged.

#### A0 and A1 throughput, glmark2

`glmark2-wayland --fullscreen` supplied a 3840 x 2160 buffer, so A1 exercised
the native-size bypass. Three alternating repeats per case, fixed scenes at
eight seconds each, after one warm-up run. The effect was loaded and unloaded
over `org.kde.kwin.Effects` between runs rather than through saved settings.

| Scene | A0 median FPS, effect unloaded | A1 median FPS, effect loaded |
| --- | --- | --- |
| texture | 20863 | 22603 |
| shading | 23078 | 23957 |
| build | 20421 | 36666, bimodal |

No overhead from the loaded effect was observed, which matches the expectation
that a native-size buffer never becomes a candidate and never blocks direct
scanout. The result does not yet bound that overhead: A0 always ran first, and
both stable scenes favour A1 by four to eight per cent, which is not a physical
outcome and indicates clock or thermal drift across the sequence. Phase-reversed
repeats are required before quoting a number.

The `build` scene alternated between roughly 20000 and 37000 FPS independently
of the effect state, so glmark2's aggregate score is unusable on this host.
Report per-scene medians, as the measurement protocol already requires.

Runs B, C and D were not measured. `--fullscreen` selects the output size, and
no verified route to a smaller fullscreen game buffer exists on KWin 6.3.6, so
the cost of EASU and of losing direct scanout remains unmeasured. This is the
[resolution-control](slice-resolution-control.md) dependency, observed here as
a direct block on this slice's cost acceptance rather than as a future risk.

#### The effect rejects an eligible supplied buffer on this session

A standalone fixture built from this repository's `autotests/wayland_client.cpp`
committed a shared-memory buffer with a fullscreen viewport, which is the case
the container integration test covers. The effect reported the correct supplied
and destination sizes and still refused the window:

| Buffer | Destination | Desktop scale | Reported state |
| --- | --- | --- | --- |
| 1920 x 1080 | 3840 x 2160 | 3 | Ineligible |
| 2560 x 1440 | 3840 x 2160 | 3 | Ineligible |
| 1920 x 1080 | 3840 x 2160 | 1 | Ineligible |

`activeEffects` never listed `upscale`, so no scanout restriction was taken and
no scaled frame was produced. KWin's own scripting interface confirmed that the
window was fullscreen, had frame geometry equal to the output geometry, opacity
one and the expected output, so the window-level conditions in `eligible()` all
held. The rejection is therefore in the surface-level conditions.

Desktop scale is ruled out: the scale 1 run reproduced the container test's
geometry exactly and still failed. Buffer size is ruled out by the two sizes
above. The remaining candidates are the surface child-item, transform, source
box, opaque-region and buffer-format checks, and the difference between this
session's OpenGL and DRM backend and the container fixture's QPainter backend.
The cause is not isolated; no fix is claimed.

This is the first time the installed effect met a supplied buffer it was built
to scale on real hardware, and it did not scale it. The passing container and
headless tests did not predict this, which is a coverage gap in those tests as
well as a defect somewhere in eligibility. Isolating it requires a diagnostic
effect that reports the failing condition, as the resolution-control experiments
used. Until it is resolved, no rendering, image-quality or cost acceptance can
proceed on this host.

### Required acceptance matrix

Record each actual run here with its environment, result and any remaining
limitation. The following are required checks, not claims of success:

| Area | Required evidence |
| --- | --- |
| Repository checks | `pre-commit run --all-files` in the container; inspect the output. |
| Builds | Trixie and neon unstable, GCC and Clang, warnings as errors; GLSL and GLSL ES shader validation. |
| Runtime | Tests against KWin's virtual backend in the container; actual original-buffer sizes and pixel mapping. |
| Configuration | Percentage-to-pixel conversion at different output and desktop scales; exact preset ratios, rounding, aspect tolerance, 50% and native boundaries, RCAS bypass and increasing strength. |
| Real game | Complete the OpenGL/Vulkan benchmark and open-source game stages first, then Steam/Epic games with Valve Proton and standalone Wine. On the TV, prove smaller actual buffers and a 4K destination, including 1080p and 1440p; cover the required Windows graphics paths and record runtime versions. |
| Image quality | Compare KWin scaling, EASU, and EASU plus RCAS on identical input; inspect text, HUD, fine edges and camera movement with Jens. |
| HDR | SDR to SDR, SDR to HDR, PQ and scRGB to HDR, and transitions; gradients, highlights, wide-gamut colours and negative scRGB values without accidental clipping or duplicate colour conversion. |
| VRR | Verify adaptive presentation with active scaling and blocked direct scanout, both in SDR and HDR, across changing frame rates within the actual output range. An enabled setting alone is insufficient. |
| Cost | Execute the A0–D benchmark matrix for OpenGL and Vulkan: native resolution, native-size bypass, ordinary scaling, EASU, and EASU with RCAS. Report repeated throughput and fixed-frame-rate cost measurements, variation and relative differences, including the loss of direct scanout. Fixed 60 Hz is one baseline; test 120 Hz where the output path supports it. |
| Lifecycle | Native resolution, windowed mode, unsupported geometry, resource failure, output changes and deactivation preserve normal rendering, cursor and overlays; no stale textures or persistent scanout restriction. |

A driver or display-link limitation leaves the corresponding hardware test
open. It does not remove HDR or VRR from the scope. Falling back for every
HDR input is not HDR upscaling support, and virtual tests do not replace
real-display acceptance.

The native scaler build is installed on wzpc but has not yet been timed or
accepted on the TV.
Documentation checks do not constitute completion of the slice.
