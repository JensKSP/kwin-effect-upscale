<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: FSR 1 upscaling with HDR and VRR

Status: implementation in progress; real-device acceptance will be performed
with Jens in a later session. This temporary
working document covers this major slice's scope, plan, progress, findings,
TODOs and test results. The permanent requirements and specification live in
the [upscaling developer handbook](upscaling.md).

Update this document throughout implementation. Delete it and update its links
only after implementation, required tests and real-device acceptance are
complete. Keep the handbook current and put durable implementation explanations
in source comments and tests; the handbook is not deleted with this slice.

## Scope and selected approach

The first implementation will use **FSR 1: EASU with optional RCAS**, starting
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

The configuration includes the handbook's percentage slider with pixel
preview, FSR resolution presets, automatic mode, optional RCAS strength and
separate desired/actual resolution status. A wish is guidance unless a verified
client request path exists; use a supported path to attempt the desired size.
Always adapt scaling to the actual buffer, including games with fixed sizes.
A mismatch with the wish alone must not disable scaling. The wish is never
implemented by downscaling a completed frame. HDR and VRR remain mandatory
throughout.

## Integration questions to resolve

1. Can an effect in 6.3.6 reach the surface texture at **buffer size**, or does
   the offscreen path always deliver it at target size?
2. How does blocking direct scanout interact with tearing, HDR and adaptive
   sync? Blocking it unconditionally would cost more than the scaling gains.
3. Xwayland games — the Proton default — on 6.3.6, which lacks master's RandR
   resolution emulation (`_XWAYLAND_RANDR_EMU_MONITOR_RECTS`,
   `src/x11window.cpp:3469-3474`): does a 1080p fullscreen window arrive as a
   small buffer with a 4K target size?
4. Does KWin reject a plugin built against a different effect API version
   cleanly? 6.3.6 declares version `0.236` (`src/effect/effect.h:101-104`); the
   place where it is checked at load time has not been found yet.

5. Can a native Wayland client's preferred scale be requested safely from an
   effect on KWin 6.3.6, without fighting the compositor's own scale policy?
   Verify protocol quantisation, actual buffer commits and restoration. Test
   Proton/Xwayland independently; unsupported requests remain clearly labelled
   in-game guidance.

Question 1 decides whether the effect is possible at all in the supported
version. Question 4 decides how packaging has to deal with KWin updates.

## Implementation plan

1. Establish access to the original buffer, its source rectangle and colour
   description in KWin 6.3.6. Use a known pixel pattern to detect accidental
   sampling of an already enlarged image. Check the shader capabilities.
2. Establish the HDR colour path and VRR behaviour during active composition
   before committing to the integration. HDR and VRR are requirements of this
   slice, including their simultaneous use.
3. Confirm that a real Proton/Xwayland game supplies a smaller buffer with a
   4K destination. Keep the session on Wayland. Record an unavailable path as
   a blocker instead of introducing resolution spoofing.
4. Implement EASU through KWin's OpenGL abstractions, initially using FP32
   fragment shader arithmetic. Reuse intermediate textures and avoid CPU
   readback. A controlled SDR path is an intermediate step; complete the HDR
   conversions and choose intermediate formats with sufficient range and
   precision within this slice.
5. Implement the configuration and status described in the handbook. Keep
   calculated wishes, requests and actual buffer dimensions separate. Test
   presets, rounding and output changes; investigate cooperative client hints
   without assuming that Proton games honour them.
6. Add optional RCAS. Off must bypass sharpening, since AMD's numeric zero
   means maximum sharpening. Keep separate overlays and the cursor out of the
   game scaler. Document colour assumptions and conversion boundaries beside
   the shader code.
7. Handle resizing, output changes, deactivation and resource failures.
   Release redirection and any additional direct-scanout restriction when the
   effect becomes inactive. Preserve KWin's event-driven presentation and VRR;
   do not add a frame timer or continuous repaint loop.
8. Complete the checks and acceptance below. Put lasting invariants and
   explanations in source comments, update the handbook, then remove this slice
   document and its links.

FP16 arithmetic, compute shaders, pass fusion and caching scaled results are
later optimisations driven by measurements. The initial geometry is one
eligible fullscreen window on one output, with the full buffer visible and
no buffer transform. Multiple simultaneous candidates fall back to KWin.
Use physical pixel sizes and capability checks, not fixed resolutions or GPU
vendor checks.

## Findings

- KWin 6.3.6's `OffscreenEffect` captures at output scale into `GL_RGBA8`
  with an sRGB description. It cannot supply the required input unchanged.
  The implementation will instead render the surface item at buffer resolution
  through KWin's item renderer, retaining its import and release-fence handling.
- Colour conversion to the current render target will happen at input
  resolution. Floating-point intermediates and a reversible working encoding
  are required before EASU/RCAS; final composition must not convert colours a
  second time. This encoding still requires HDR image-quality acceptance.
- KWin 6.3.6 selects adaptive presentation in `compositor_wayland.cpp`
  independently of direct scanout. This establishes an integration path, not
  proof of VRR on a physical display.
- Preferred-buffer-scale hints share ownership with KWin's output policy.
  Until restoration and cooperative clients have been verified, resolution
  wishes remain game-setting guidance and send no protocol requests.

- The implementation in `src/plugins/upscale/` currently reports itself
  inactive and does not scale anything. Shader integration and runtime
  acceptance remain open.
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
listed in the [README](../README.md).

The fractional-scale protocol allows a compositor to suggest a surface scale;
KWin 6.3.6 exposes a preferred-buffer-scale setter. Neither establishes safe
scale-policy ownership by this effect or general Proton game support. Sources
and the intended user-visible behaviour are recorded in the handbook's
[configuration section](upscaling.md#configuration). No client negotiation has
been implemented or tested.

## Progress and remaining work

- [x] Select FSR 1 with optional RCAS as the initial implementation approach.
- [x] Make HDR and VRR mandatory, including combined use while scaling.
- [x] Review gamescope and alternative spatial filters; record findings and
  references.
- [x] Separate this slice's working record from the permanent developer handbook.
- [x] Specify percentage and pixel controls, presets and honest resolution status.
- [ ] Implement the configuration, pixel preview and actual-buffer status.
- [ ] Verify cooperative resolution requests and manual game-setting guidance.
- [ ] Resolve the KWin integration questions above, including HDR and VRR.
- [ ] Implement and comment the buffer path, EASU and optional RCAS.
- [ ] Implement HDR colour handling and preserve adaptive presentation.
- [ ] Test lifecycle changes, fallback behaviour and inactive operation.
- [ ] Complete container checks, native measurements and TV acceptance.
- [ ] Update the handbook and source comments with lasting conclusions.
- [ ] Remove this slice document and update its links after completion.

## Validation and acceptance

Record each actual run here with its environment, result and any remaining
limitation. The following are required checks, not claims of success:

| Area | Required evidence |
| --- | --- |
| Repository checks | `pre-commit run --all-files` in the container; inspect the output. |
| Builds | Trixie and neon unstable, GCC and Clang, warnings as errors; GLSL and GLSL ES shader validation. |
| Runtime | Tests against KWin's virtual backend in the container; actual original-buffer sizes and pixel mapping. |
| Configuration | Percentage-to-pixel conversion at different output and desktop scales; exact preset ratios, rounding, aspect tolerance, 50% and native boundaries, RCAS bypass and increasing strength. |
| Resolution wish | Automatic makes no request; unsupported control remains guidance; verify cooperative, ignored and adjusted requests, including a game with a fixed resolution different from the wish. Scaling uses actual buffer dimensions. Check output changes and cleanup without repeated requests or continuous repaints. |
| Real game | Native build on the TV, with a smaller game buffer and 4K destination; 1080p and 1440p patterns, plus the resolutions offered by the game. |
| Image quality | Compare KWin scaling, EASU, and EASU plus RCAS on identical input; inspect text, HUD, fine edges and camera movement with Jens. |
| HDR | SDR to SDR, SDR to HDR, PQ and scRGB to HDR, and transitions; gradients, highlights, wide-gamut colours and negative scRGB values without accidental clipping or duplicate colour conversion. |
| VRR | Verify adaptive presentation with active scaling and blocked direct scanout, both in SDR and HDR, across changing frame rates within the actual output range. An enabled setting alone is insufficient. |
| Cost | Measure total GPU rendering time, frame times and power at comparable frame rates, including the loss of direct scanout. Fixed 60 Hz is one baseline; test 120 Hz where the output path supports it. |
| Lifecycle | Native resolution, windowed mode, unsupported geometry, resource failure, output changes and deactivation preserve normal rendering, cursor and overlays; no stale textures or persistent scanout restriction. |

A driver or display-link limitation leaves the corresponding hardware test
open. It does not remove HDR or VRR from the scope. Falling back for every
HDR input is not HDR upscaling support, and virtual tests do not replace
real-display acceptance.

No scaler implementation has yet been built, timed or accepted on the TV.
Documentation checks do not constitute completion of the slice.

Previous documentation check (before separating handbook and slice):
`pre-commit run --all-files` passed in the Trixie container on a working-tree
copy under `build/` for the documentation and workflow changes. This run does not establish build or runtime acceptance of the scaler.

After separating the handbook and slice document, `pre-commit run --all-files`
passed in the Trixie container on a working-tree copy under `build/`, including
the new slice document. Local Markdown links also resolved successfully.
Implementation, builds and runtime acceptance remain open.

After specifying the resolution controls and best-effort wish behaviour,
`pre-commit run --all-files` passed in the Trixie container on a working-tree
copy under `build/`. This checks the documentation; percentage controls and
client negotiation still require implementation and runtime tests.
