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

The configuration includes the handbook's percentage slider with pixel
preview, FSR resolution presets, automatic mode, optional RCAS strength and
separate desired/actual resolution status. Resolution wishes remain guidance
here; verified client requests belong to the resolution-control package.
Always adapt scaling to the actual buffer, including games with fixed sizes.
A mismatch with the wish alone must not disable scaling. The wish is never
implemented by downscaling a completed frame. HDR and VRR remain mandatory
throughout.

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

- [x] Select and implement FSR 1, optional RCAS and global configuration.
- [x] Implement colour conversions and record automated shader/configuration tests.
- [ ] Complete original-buffer and lifecycle integration acceptance.
- [ ] Measure the complete rendering path in the staged benchmark matrix.
- [ ] Complete real-game, HDR/VRR, image-quality and TV acceptance.

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
