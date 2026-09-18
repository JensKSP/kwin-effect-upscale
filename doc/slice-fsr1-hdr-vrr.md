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
3. Confirm that native Wayland and Proton/Xwayland games supply smaller buffers
   with a 4K destination. Keep the session on Wayland and KWin unpatched.
   Investigate per-game resolution advertising as specified in the handbook;
   record unavailable control paths and actual buffer behaviour separately.
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
[configuration section](upscaling.md#configuration). The production effect
sends no client requests; isolated resolution-control experiments are recorded below.

## Progress and remaining work

### Resolution-control investigation, 2026-09-18

Wayland is the primary session and native Wayland and Xwayland games are both
required. KWin must remain unpatched. The handbook now permits investigation
of per-game fullscreen resolution advertising and, if necessary, a virtual
output; the earlier blanket exclusion of resolution spoofing no longer applies.
These are investigation findings, not an implemented resolution-control path.

Observed source and binary evidence:

- In KWin 6.3.6, `EffectWindow::window()` reaches `Window`; application identity,
  geometry requests and output assignment are accessible. Native application
  IDs populate the resource class in `XdgToplevelWindow::handleWindowClassChanged()`.
- Inspected the exported symbols with `nm -D -C` in the existing Trixie and
  neon project containers. Packages were `kwin-dev` version `4:6.3.6-1` and
  `4:6.7.5+p24.04+vunstable+git20260915.1132-0`, respectively. Both export
  `OutputBackend::createVirtualOutput()`, `Window::moveResize()`,
  `Window::sendToOutput()`, `Window::setNextTargetScale()`,
  `OutputInterface::clientResources()` and input-filter installation.
  This verifies symbol availability, not successful calls or runtime behaviour.
- KWin 6.3.6's `SurfaceInterface::setPreferredBufferScale()` sends fractional
  hints in units of 1/120 and rounds up for the integer protocol. Fullscreen
  configure events resend KWin's target scale. A one-time surface override can
  therefore be overwritten; scale ownership and restoration remain unresolved.
- `OutputInterface::clientResources()` exposes per-client output resources,
  but the reviewed public API has no complete per-game mode-override operation.
  Sending a mode event alone does not change fullscreen geometry, logical
  output information or the game's chosen buffer size.
- Xwayland mode emulation belongs to the requesting X client. A separate mode
  request cannot be assumed to affect the game. The reviewed master KWin source
  handles `_XWAYLAND_RANDR_EMU_MONITOR_RECTS`; 6.3.6 lacks that integration.
- KWin's 6.3.6 DRM backend creates ordinary virtual outputs with a software
  60 Hz presentation clock. Its base backend implementation returns null;
  support must be checked. An extra output is not a private screen for one game.
- `Window::inputTransformation()` and hit testing use window geometry rather
  than effect paint transforms. Scaling a small window visually requires
  separate input handling. Physical-output VRR policy also considers which
  output owns the active window (`compositor_wayland.cpp`).

Remaining investigation: validate native scale-policy ownership across output
changes and effect lifecycle; resolve the Xwayland fullscreen compatibility gap
on the minimum supported KWin; determine whether consistent per-game display
advertising can be implemented through existing APIs; test game identity,
launch-time mode caching, input, cleanup, HDR and VRR. Game-resolution enforcement
and virtual-output presentation remain unaccepted. The runtime probes below
establish narrower results with controlled clients.

#### Resolution-control experiments on both KWin versions

A separate temporary effect, a Qt Widgets client, a cooperative fractional-scale
client and an XCB RandR client were built under `build/resolution-probe/`.
The final experimental sources compiled with warnings as errors using GCC 14
and Clang 19 in Trixie and GCC 13 and Clang 18 in neon. These are probe builds,
not verification of the production scaler. Neither KWin installation was patched.

Runtime experiments used a 3840 × 2160 virtual output at desktop scale 1.
Trixie used QPainter; neon's virtual backend required OpenGL and access to the
GPU render node. No physical display-control device was passed to the containers.
The observed results were:

| Experiment | Trixie, KWin 6.3.6 | neon, KWin 6.7.5 development package |
| --- | --- | --- |
| Cooperative native client; next target scales 0.5, 0.75, then 1 | Buffers changed to 1920 × 1080, 2880 × 1620, then 3840 × 2160; fullscreen frame and surface destination stayed 3840 × 2160. | Same buffer and geometry results. |
| Qt Widgets native client; next target scales 0.5 and 0.75 | Continued supplying 3840 × 2160 at device pixel ratio 1. | Same result with the container's Qt 6.11.1. |
| Qt Widgets native client; send only smaller current/preferred `wl_output.mode` events to its bound output resource | Screen geometry, fullscreen window and buffer stayed 3840 × 2160. | Same result. |
| XCB client requests an emulated 1920 × 1080 RandR mode on its own connection, then enters fullscreen | Request succeeded, but KWin configured 3840 × 2160 and received a 3840 × 2160 buffer. | KWin configured 1920 × 1080 and received that buffer with a 3840 × 2160 surface destination and fullscreen frame. |

The XCB test queried the CRTC from both the requesting connection and a separate
observer. The requester saw the emulated mode ID while the observer retained
the original mode ID, on both versions. The CRTC width/height fields remained
3840 × 2160 even for the requester; mode IDs, window configures and committed
buffers were therefore recorded separately. The request was made by the test
client itself, not by an effect on behalf of another application.

A Qt Widgets Xwayland client on Trixie also kept its 3840 × 2160 buffer when
`Window::setNextTargetScale()` requested 0.5 and 0.75. The XCB mode test's buffer
sizes likewise remained unchanged by those subsequent scale requests. A native
Wayland scale request is not an Xwayland resolution-control mechanism.

A native build of the cooperative-client demonstration also ran in nested,
unmodified KWin 6.3.6 on wzpc. Logs confirmed the same 4K, 1080p, 2880 × 1620,
and restored 4K buffers. The existing TV session was a Sway kiosk: its fullscreen
main menu initially covered the test. A later run brought the nested compositor
fullscreen using the kiosk's IPC and restored the menu afterwards. The IPC
reported successful focus/fullscreen changes and a final query confirmed the
menu was focused and fullscreen again. Jens reported seeing only the menu;
the IPC results do not establish that the test reached the TV. Visible
acceptance therefore remains open. This demonstration used ordinary KWin
scaling, not the FSR effect; it establishes neither image-quality nor HDR/VRR
acceptance.

Conclusions: no KWin patch is needed to request smaller buffers from a
cooperative native client. Neither that hint nor a single advertised-mode event
forces an arbitrary client to change resolution. Xwayland's existing mode
emulation works for the tested client on neon, but the same test fails to
produce a smaller fullscreen buffer on the minimum supported KWin. This is a
compatibility issue to resolve, not grounds to silently drop Xwayland or 6.3.6.
Actual games, launch-time mode caching, fractional desktop scales, input,
output changes and lifecycle restoration still require acceptance. These
experiments did not implement a private virtual display or a universal override.

Documentation checks: `pre-commit run --all-files` and its `pre-push` stage
passed in the Trixie container against the documentation changes. The first
regression run failed because the isolated check copy lacked Git history;
using the existing repository history resolved that test-setup error.

### Wine, Valve Proton and existing Wayland solutions, 2026-09-18

Scope: investigate a launch-time display override for native Linux games,
upstream Wine and Valve Proton without patching KWin. Treat the X11 and native
Wayland Wine drivers as separate paths, and do not substitute GE-Proton for
Valve Proton acceptance. Existing software and its implementation are part of
the investigation.

Planned checks: inspect upstream display enumeration, mode-change emulation,
virtual desktops and surface presentation; compare existing Wayland solutions;
run isolated Windows display-API probes where runtimes are available. Measure
what the application sees separately from the buffer delivered to KWin. A
solution must preserve the physical output mode, input mapping and restoration,
and must deliver an original smaller buffer to the effect. Real games, HDR and
VRR remain separate acceptance requirements.

Session observation: KDE Wayland is now active on wzpc, KWin 6.3.6 reports the
`upscale` effect loaded, and HDMI-A-1 has logical geometry 1280 × 720 at scale 3
and 120 Hz. This is a 3840 × 2160 physical destination. No session setting was
changed for this inspection. Wine was not available on the terminal's PATH;
Valve Proton 11.0-2 was selected as the latest stable release after checking
Valve's release list. Its Wine submodule is
`dc26e61847081a1b5cb0733dc30feba6ee575482`. No Proton runtime was found in the
checked default Steam library paths; Proton execution and real-game acceptance
remain pending identification of the Steam installation.

#### Observed Wine results

A Windows GDI test program and a read-only KWin observer ran in a disposable
Trixie container, using Wine `10.0~repack-6`, KWin 6.3.6, QPainter and a
3840 × 2160 virtual output at scale 1. The observer made no resolution requests.
The program queried `GetSystemMetrics` and `EnumDisplaySettings`, optionally
requested 1920 × 1080 with `ChangeDisplaySettings`, and painted a borderless
window at the size Windows reported. All prefixes and probe sources are under
`build/wine-resolution-research/`; no installed game prefix was modified.

| Test | Windows observation | KWin observation |
| --- | --- | --- |
| Xwayland baseline | 3840 × 2160 | Fullscreen; 3840 × 2160 buffer and destination. |
| Xwayland mode request | The API returned success, but current mode and screen metrics remained 3840 × 2160, including after eight seconds. | Fullscreen; still a 3840 × 2160 buffer and destination. |
| Native Wayland mode request | Screen metrics and current mode changed to 1920 × 1080; restoration returned them to 3840 × 2160. | A 3840 × 2304 backing buffer with a 3840 × 2160 destination; KWin did not mark this test window fullscreen. No smaller scaler input. |
| Xwayland named virtual desktop at 1920 × 1080 | The child program reported 1920 × 1080 throughout. | After initial resize, the desktop supplied a 1920 × 1080 buffer and destination in a decorated 1928 × 1113 frame. It was not fullscreen. |

The first native run stopped when Wine requested pointer locking without a
pointer device. The completed native test registered a virtual input device;
KWin's interface-permission test override was confined to that disposable
headless session. The first virtual-desktop launcher failed to start its child
because of path escaping; the completed run used a corrected argument and
file-based child measurements. These setup failures are not game results.

Wine's virtual desktop therefore demonstrates the desired Windows-visible
size and smaller buffer, but still needs fullscreen presentation and input
mapping. The native GDI result demonstrates why Windows mode success alone is
insufficient. Neither test establishes Vulkan, DXVK, vkd3d-proton, HDR or VRR
behaviour, and neither is a Valve Proton runtime test.

The source survey found Gamescope, Sommelier, waywall, Wine virtual desktops,
and GE-Proton's custom FSR mode. The handbook records links and distinctions.
Sommelier at `3d7104654150b0759fbdeb271148ba8da81f5a23` translates both output
sizes and `xdg_toplevel.configure` sizes; its architecture delegates composition
to the host. A similar launch helper with a private Xwayland instance is a
candidate for consistent per-game display information while retaining smaller
buffers for KWin. It is not implemented or tested here. Valve Proton 11.0-2's
Vulkan source also contains a host-size blit path: measure which path is used
rather than assuming Windows display emulation leaves upscaling to KWin.

Next experiments: verify a proxy forwards original small buffers and viewport
mapping on unpatched KWin; exercise Wine 10.0 and official Proton 11.0-2 with
Vulkan/DXVK and D3D12; compare borderless and exclusive fullscreen; verify
input, subsurfaces, launcher children, output scaling, restoration, HDR and VRR.
No universal effect-only forcing path has been accepted.

#### Container toolchain follow-up

The Wine observer exposed a missing Ninja executable in the cached Trixie
check image. CMake is already a build dependency; both maintained Containerfiles
already install GCC through `build-essential` and install Clang explicitly.
Ninja was added to `debian/control`, the shared dependency list. Both image
builds now fail if CMake, Ninja, GCC or Clang cannot run. The maintained images
were rebuilt successfully, and a C++23 program configured with Ninja, compiled,
linked and ran under both compilers in each image, with warnings as errors:

| Image | CMake | Ninja | GCC | Clang |
| --- | --- | --- | --- | --- |
| Trixie | 3.31.6 | 1.12.1 | 14.2.0 | 19.1.7 |
| neon unstable | 3.30.5 | 1.11.1 | 13.3.0 | 18.1.3 |

This verifies toolchain availability, not a build of the concurrently modified
production effect. Wine experiment packages remain confined to the disposable
research container. Rebuilding is required to update existing cached images.

### Implementation checklist

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
