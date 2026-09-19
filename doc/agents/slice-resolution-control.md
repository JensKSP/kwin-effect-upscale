<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: obtaining smaller game buffers

## Priority

This package owns the project's feasibility gate, as decided on 2026-09-18.
Without a supported way to obtain a smaller original buffer, the effect has
nothing to enlarge for a game that does not already supply one, and the
remaining packages configure, announce and measure a path that never runs.

It is sequenced third, after the
[development infrastructure](slice-development-infrastructure.md) package and
the rendering package's
[scaler-effective gate](slice-fsr1-hdr-vrr.md#the-scaler-effective-gate). That
order is deliberate. On 2026-09-18 the effect refused a buffer that met every
documented eligibility rule on real hardware, so a helper delivering smaller
buffers today would deliver them to a scaler that declines them. Building the
delivery mechanism before the consumer is known to work risks attributing its
failure to the wrong component, and the helper is the most expensive component
in the project. Feasibility gate and next package are not the same thing.

Nothing here is deferred by that ordering. The experiments below already stand,
and the recorded decision cannot wait indefinitely: every dependent package is
specified against capabilities this one may never deliver.

Two earlier results made this the gate rather than one feature among several.
The [rendering slice](slice-fsr1-hdr-vrr.md) could not measure runs B, C and D
on real hardware because it had no verified route to a smaller fullscreen game
buffer on KWin 6.3.6, so the cost of the scaler and of losing direct scanout is
still unknown. The later [X11 investigation](#per-application-x11-buffer-control-investigation-2026-09-19)
established such a route in a research effect on the minimum supported version,
including [Debian installation and basic restoration](#installed-package-and-lifecycle-results).
Production integration and hardware acceptance remain open.

This package must therefore reach one of three stated outcomes rather than
remaining open indefinitely:

| Outcome | What it commits the project to |
| --- | --- |
| Cooperative clients only | Integrate verified native Wayland requests and targeted X11 resizing for their stated client classes, report other clients honestly as unsupported, and drop the launch helper from the required scope. |
| Ship a launch helper | Adopt the protocol proxy or the Gamescope Wayland backend as a second delivered component, with its own surface-tree, input, lifetime and packaging work. |
| Guidance only | Send no request at all, keep the desired size as in-game guidance, and accept that the effect serves buffers the user reduced by hand. |

Each outcome is a defensible product. Leaving the choice open is not: it keeps
every dependent package specified against capabilities that may never exist.
Record the selected outcome and its reasoning here, then update the handbook's
[resolution-control direction](../upscaling.md#resolution-control-direction-after-the-experiments).

## Start state

The effect scales supplied buffers and shows a desired resolution, but sends
no production resolution requests. Experiments below establish several partial
routes and their failures; they do not establish universal enforcement or
accepted real-game compatibility. The physical output must stay unchanged.

## End state

Verified resolution-control methods can obtain and forward original smaller
buffers for the required native Wayland, Xwayland, Valve Proton and standalone
Wine cases on supported KWin. They report actual, adjusted, ignored and
unsupported results, preserve input/presentation, and restore owned state on
exit or deactivation. Required automated and hardware acceptance has passed.
An unsupported application is reported honestly, not counted as a successful
reduction; required compatibility cases cannot be dropped to close this slice.

## Supported scope and full acceptance

The [handbook](../upscaling.md#supported-scope-and-full-acceptance) defines both
gates. For this package they are:

**Supported scope, releasable.** One verified method obtains a smaller original
buffer for a stated client class on KWin 6.3.6, reports actual, adjusted,
ignored and unsupported results truthfully, owns and restores the state it
changes, and leaves every other client on its normal resolution policy. The
cooperative native Wayland request is the candidate already demonstrated on
both versions. Closing this gate requires that method's ownership and
restoration to be settled across output changes and effect lifecycle, and the
selected outcome from the priority section above to be recorded.

**Full acceptance.** The criteria below, unchanged: Xwayland on the minimum
supported version, Valve Proton and standalone Wine across the required
graphics paths, helper-forwarded surface trees, input, HDR, VRR and real-game
hardware acceptance.

Shipping the supported scope does not resolve the Xwayland gap on KWin 6.3.6 or
the Proton and Wine cases. Those stay required, keep this document open, and
must be named as unsupported in the release that ships without them.

## Scope and boundaries

Own cooperative negotiation, private-display/helper integration, forwarding
original buffers including necessary surface-tree support, input mapping and
restoration. Implement the verified method capabilities and result reporting
needed by callers. Keep KWin unpatched and retain the 6.3.6 target.

Application matching belongs to [profiles](slice-application-profiles.md).
Launch definitions, launcher adapters, restart orchestration and automated
method discovery belong to [launching](slice-application-launching.md).
Image quality and filter performance belong to [rendering](slice-fsr1-hdr-vrr.md).
Geometry for different aspect ratios and windowed-game presentation are outside
this package. Their absence does not justify stretching or changing the output.

## Dependencies

Use the existing supplied-buffer scaler and explicit selection of the target
application. Integrate with the profile identity contract when available.
Publish a method interface for the launching package: capabilities, live versus
launch-time application, actual result, owned-resource cleanup and restoration.
Method probes can proceed independently of the profile editor and launch UI;
complete end-to-end cases use those integrations when required.

## Approach

1. Resolve the minimum-version Xwayland issue and native scale-policy ownership
   using the recorded experiments; select only verified routes.
2. Implement method capability checks and original-buffer forwarding without
   downsampling a completed native-size image.
3. Verify input, surface trees, fullscreen transitions, output/desktop scaling,
   cleanup, HDR and VRR for each supported method.
4. Integrate method selection and truthful target/actual state with the shared
   configuration contract. Automatic supplied-buffer mode makes no request.
5. Complete the handbook's game-control acceptance; retain unsupported cases
   and measured limitations in human documentation and implementation comments.

## Acceptance criteria

Planned checks, not observed results:

- Cooperative, ignored and adjusted live requests, ownership and restoration;
  Automatic makes no request and explicit methods do not silently fall back.
- Original 1920 × 1080 and 2560 × 1440 buffers reach a 3840 × 2160 destination
  without changing the physical output or unrelated applications. Cover native
  OpenGL/Vulkan and the required Wine/Proton graphics paths.
- Follow the handbook's [game-control tests](../upscaling.md#game-identification-and-resolution-control-tests)
  with Extreme Tux Racer and SuperTuxKart, then Steam/Epic cases in the required
  order. Manual in-game reduction is a baseline, not proof of effect control.
- Cover live target changes, pending launch, unsupported negotiation, fixed-size
  clients, helpers, child surfaces, fullscreen modes, fractional desktop scales,
  multiple outputs, pointer mapping, confinement, locking and controllers.
- Verify HDR and actual adaptive presentation on hardware through each claimed
  method, and cleanup after failure, game exit, output changes or deactivation.
- Run repository checks, both compiler/container builds with warnings as errors,
  virtual-backend integration and real-game/TV acceptance. Do not reuse a probe
  build as evidence for the production effect.

## Findings and observed results

The dated observations below were retained from the original combined slice.
Their stated environments and limits still apply; moving them is not a new run.

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
changes and effect lifecycle; validate Xwayland fullscreen mode emulation on
the minimum supported KWin; determine
whether consistent per-game display advertising can be implemented through
existing APIs; test game identity, launch-time mode caching, input, cleanup,
HDR and VRR. No game-resolution enforcement or virtual-output presentation has
been accepted. The following prototype is narrower than game acceptance.

#### Native-client prototype, 2026-09-18

A temporary probe effect and a fractional-scale Wayland client ran against
unpatched KWin 6.3.6's virtual backend in the Trixie container. QPainter was
sufficient to observe protocol messages, window geometry and committed buffers;
this was not a test of the upscaling shaders or physical presentation. The probe
only addressed the test application's ID, and its sources and build remained
under the ignored `build/` directory.

- At output scale 1, the cooperative client initially committed 3840 × 2160.
  `SurfaceInterface::setPreferredBufferScale(0.5)` delivered a fractional-scale
  event of 60/120 and the client committed 1920 × 1080 into an unchanged
  3840 × 2160 viewport. Leaving fullscreen triggered a new configure and KWin
  restored the 120/120 hint; the client returned to 3840 × 2160.
- `Window::setNextTargetScale(0.5)` also produced the 60/120 hint and the smaller
  committed buffer. In this case the window's target scale became 0.5 and stayed
  there when fullscreen was restored. Its frame geometry stayed 3840 × 2160.
  Setting the next target scale back to the output's scale restored target scale
  1 and a 3840 × 2160 buffer in the final observation.
- In a second run, a client configured to ignore the hints received the same
  events but kept committing 1920 × 1080. KWin's target-scale state still changed.
  That state therefore cannot confirm that a resolution request succeeded.

These runs establish a native-client integration lead and demonstrate the
ownership problem with direct surface hints. They do not establish behaviour
at fractional desktop scales, during output changes, with actual games or
Proton/Xwayland, or under HDR and VRR. Production control still requires explicit
game selection, policy ownership and verified restoration; wishes remain
guidance in the current effect.

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
checked default Steam library paths at that stage. The later experiments below
located and executed the installed official runtime; real-game acceptance
remains open.

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
buffers for KWin. Subsequent runtime experiments are recorded below. Valve Proton 11.0-2's
Vulkan source also contains a host-size blit path: measure which path is used
rather than assuming Windows display emulation leaves upscaling to KWin.

Next experiments: verify a proxy forwards original small buffers and viewport
mapping on unpatched KWin; exercise Wine 10.0 and official Proton 11.0-2 with
Vulkan/DXVK and D3D12; compare borderless and exclusive fullscreen; verify
input, subsurfaces, launcher children, output scaling, restoration, HDR and VRR.
No universal effect-only forcing path has been accepted.

### Resolution-control acceptance experiments, 2026-09-18

Continue beyond source review. Required experiments are: force the existing
Wine 1080p desktop fullscreen and measure whether its buffer grows; inspect and
run a protocol-translating wrapper; exercise native Linux OpenGL and Vulkan
clients, then equivalent Wine and official Proton paths. Test at least 1080p
and 1440p on a 4K destination, input mapping, restoration and an application
that ignores resize requests. All experimental code and environments stay
under `build/`. An exact-size presentation buffer must not be confused with
reduced internal rendering or a downsample of a larger game buffer. Do not
claim universal enforcement from a finite list of cooperating applications.

#### Environment and reproducible launch shapes

Observed on unpatched KWin 6.3.6 in disposable Trixie containers, using its
virtual backend with a 3840 × 2160 output, normally at desktop scale 1. A
read-only diagnostic effect recorded fullscreen state, root buffer and
viewport destination sizes, plus child surfaces. Mesa 25.0.7 used the AMD
GFX1151 device. These short graphics runs establish buffer dimensions, not
benchmark scores, HDR, VRR or TV acceptance. No production plugin or real
session setting was changed.

Sommelier was built from the reviewed commit
`3d7104654150b0759fbdeb271148ba8da81f5a23`. The working local forwarding command
used `--noop-driver --display=wayland-0 --direct-scale --scale=0.5`, with
`--enable-linux-dmabuf` for graphics and `--glamor -X` for a private Xwayland.
For 1440p, the scale argument was `0.6666666666666667`. Ordinary `--scale`
without direct-scale mode did not reduce the tested native OpenGL buffer.

Gamescope `3.16.22+ds-1~bpo13+1` came from Trixie backports' contrib component.
Runs used `--backend wayland -w WIDTH -h HEIGHT -W 3840 -H 2160 -F linear -f`,
plus `--expose-wayland` for native clients. `/usr/games` must be on `PATH` for
Debian's `gamescopereaper`. The container needed both GPU nodes visible;
render-node-only access failed initialization. All launches explicitly used
the nested Wayland backend, not the physical-output backend.

Wine remained Trixie's `10.0~repack-6`. Official Proton was found in the gaming
user's Steam library: `1788504981 proton-11.0-2c-x86_64`. A copy ran through
its `proton run` launcher with isolated compatibility data. The native Steam
client library also had to be copied into the container's SDK location;
without it the launcher asserted and some runs timed out after presenting.
Corrected runs below exited normally. This used the official runtime and
DXVK/vkd3d-proton, but did not launch an installed Steam game through the Steam
client or Steam Linux Runtime container. No original game prefix was modified.
No native Wayland Wine driver was found in this installed Proton runtime.

#### Results

All sizes in the supplied-buffer column are pixels. A 4K destination means
3840 × 2160 on the unchanged virtual output. Windows probes queried screen
metrics and actual D3D texture descriptions, then cleared and presented frames
for eight seconds. D3D11 and D3D12 probes used borderless Windows windows;
exclusive fullscreen remains a separate test.

| Route and client | Supplied buffer observed at KWin | Result and limitation |
| --- | --- | --- |
| Wine virtual desktop, forced fullscreen by diagnostic effect | 3840 × 2160 root | Windows continued reporting 1920 × 1080. This does not supply smaller input. |
| Unmodified Sommelier, native glmark2 OpenGL | 1920 × 1080 and 2560 × 1440 roots | Each fullscreen, with a 4K destination. glmark2 also reported matching render-surface dimensions. |
| Unmodified Sommelier, native vkmark Vulkan cube | 1920 × 1080 root | Fullscreen, 4K destination; actual scene rendered. An earlier invalid scene name rendered nothing and is excluded. |
| Unmodified Sommelier, glmark2 through private Xwayland | 1920 × 1080 root | Fullscreen, 4K destination. |
| Unmodified Sommelier, Wine/Xwayland D3D11 | 1920 × 1080 and 2560 × 1440 roots | Windows screen metrics and D3D backbuffer matched; presentation succeeded and outer window was fullscreen. This used Wine's D3D implementation, not DXVK. |
| Unmodified Sommelier, Qt native Wayland | 3840 × 2160 root | Direct-scale mode advertised a smaller screen but fractional-scale hints requested double density. Qt still supplied 4K; shutdown also crashed. |
| Prototype Sommelier, Qt native Wayland | 1920 × 1080 and 2560 × 1440 roots | Hiding the fractional-scale global in the proxy gave scale-one rendering, fullscreen 4K coverage and clean exit. This is an experimental proxy modification, not a KWin patch or an upstream Sommelier result. |
| Gamescope Wayland backend, glmark2/Xwayland | 1920 × 1080 child | Fullscreen 4K wrapper, 1 × 1 root. The current effect rejects children and cannot use this path yet. |
| Gamescope Wayland backend, vkmark/native Wayland | 2560 × 1440 child | Fullscreen 4K wrapper, 1 × 1 root. Same integration requirement. |
| Gamescope, official Proton DXVK D3D11 | 2560 × 1440 child | Screen metrics and D3D backbuffer matched; 483 successful presents in the corrected run, exit 0. Earlier 1080p rendering succeeded but incomplete runtime setup prevented clean teardown. |
| Gamescope, official Proton vkd3d-proton D3D12 | 1920 × 1080 child | Screen metrics and backbuffer matched; 478 successful presents, exit 0. Log identified vkd3d-proton 3.0.0, build `212991fc2c266bc`. |
| Gamescope, Qt native Wayland | 646 × 513 child including decorations | Qt retained its initial 640 × 480 content despite fullscreen request. This did not reach 1080p. |
| Sommelier, official Proton DXVK | No successful D3D creation/presentation | KWin disconnected the proxy for attaching a buffer before the configure handshake. A mapped 1080p surface alone is not success. |
| Trixie Wine native Wayland D3D11, through either helper | No successful D3D device | Returned `0x887a0004`; Sommelier also reported an invalid object ID with the multi-process client. No native Wine graphics support established. |

Proton launches temporarily changed the outer Gamescope window's fullscreen
state and geometry before settling at fullscreen 4K. This is another lifecycle
case to handle before acceptance, not a continuously fullscreen guarantee.
The successful runs forwarded smaller game buffers; the production effect was
not enabled in these observer-only experiments.

#### Input, isolation and enforcement boundary

The proxy prototype mapped host pointer positions `(960, 540)`, `(1920, 1080)`
and `(2880, 1620)` to `(480, 270)`, `(960, 540)` and `(1440, 810)` at 1080p.
At 1440p it mapped them to `(640, 360)`, `(1280, 720)` and `(1920, 1080)`.
These were observed Qt pointer events, injected only into the isolated virtual
compositor. Wine pointer probes did not establish corresponding movement;
Windows input, relative motion, confinement, locking and controllers remain
open. KWin's permission-check override was used only in these isolated sessions.

An unrelated Qt client alongside the proxy retained a 4K buffer. Another Qt
client launched after proxy shutdown also supplied 4K. Neither required a
restoration of global display settings. A deliberate fixed-4K client still
supplied 4K through the prototype's virtual 1080p display: the helper cannot
force arbitrary client buffer allocation or internal rendering. A Gamescope
attempt with that probe did not map a surface and is not an enforcement result.

The resulting direction is a per-profile launch helper which forwards original
buffers, with either a protocol proxy or Gamescope's Wayland backend. Neither
is ready to ship unchanged. Sommelier exposes client-compatibility and protocol
lifetime work; Gamescope requires surface-tree support in this effect and
verification of every path that can switch to intermediate composition.
Keep cooperative scale hints and game settings as additional routes. There is
no experimentally justified universal “always reduce” guarantee.

Remaining acceptance: real OpenGL/Vulkan games, profile-driven launches and
identification, Wine/Proton input, exclusive fullscreen and mode changes,
32-bit applications, popup/overlay handling, synchronization and colour
protocols, HDR/VRR, multi-output behaviour, physical TV acceptance, and repeatable
performance comparisons. The containers and probes establish mechanisms only.
A virtual-backend `--scale=3` startup attempt did not retain an effective
scale-three desktop in the observed window state, so it does not establish
compatibility with the TV's scaled desktop.

Documentation validation: `pre-commit run --all-files` passed in the Trixie
container on a checkout containing these documentation changes and the current
committed implementation, isolated from concurrent uncommitted development.
The full pre-push stage also passed, including licensing and build/checker
regression tests. These checks do not establish a new production build.

### Resolution method settings follow-up, 2026-09-18

Specify a global resolution control method with Auto as the default and a
per-application override. The handbook now defines the candidate choices,
inheritance versus explicit Auto, separation from the preferred resolution,
verified automatic selection, explicit-method failure and restart-required
status. A detected running game can acquire a profile, but a launch helper
requires a subsequent launch. This is a specification change; method selection
and launch/profile integration remain unimplemented. Acceptance must cover
inheritance changes, explicit Auto, unavailable methods, failed negotiation,
and pending launch without automatic game restarts.

### Plugin-only control of a game the user started, 2026-09-18

Jens set the product constraint for this package: the user starts the game, the
plugin detects it, and the plugin alone obtains the smaller buffer. Launching
the game, relaunching it, and editing the user's game settings are not the
common path; a restart is acceptable only when the user deliberately chooses a
setting that needs one. The mechanism must be invisible to the rest of the
session and to the user, and the effect has to work after installing the
package without the user configuring anything.

That rules out the launch helpers measured above. Gamescope and the protocol
proxy remain recorded evidence of what forwarding can achieve, not the product
route. The experiments below therefore ask a narrower question: acting only as
a loaded KWin effect, what can be done to a game that is already connected, or
that connects while the effect is loaded?

Observed on the development machine's own KWin 6.3.6 with a native probe
effect, inside nested headless `kwin_wayland --virtual` sessions with the real
GPU (Mesa 26.1.2, RADV STRIX_HALO). Each game ran with its own runtime,
configuration and home directory; the session's own settings and the user's
game settings were not touched. Sources are under `build/game-probe/`.

#### The identities the games actually present

| Application | Package version | Backend | `resourceClass` | `resourceName` | Desktop file | Executable at connect |
| --- | --- | --- | --- | --- | --- | --- |
| SuperTuxKart | `1.4+dfsg-5+b1` | native Wayland | `supertuxkart` | `supertuxkart` | `supertuxkart` | `/usr/games/supertuxkart` |
| Extreme Tux Racer | `0.8.4-1` | Xwayland | `Extreme Tux Racer 0.8.4` | `etr` | empty | not separable |

Extreme Tux Racer puts its version number in the window class, so a profile
matching the class would stop matching after a package update. Its instance
name `etr` is the stable field. This is the reason identities are measured
rather than derived from a name.

#### Requests to a game that is already running

Each lever was applied to SuperTuxKart while it was fullscreen and committing
3840 × 2160, on an unchanged 3840 × 2160 output.

| Lever, applied through the effect | What KWin recorded | What the client committed |
| --- | --- | --- |
| `Window::setNextTargetScale(0.5)` | target scale became 0.5 | unchanged 3840 × 2160 |
| `wl_output` mode rewritten to 1920 × 1080 on the game's own resources | events delivered to one resource | unchanged 3840 × 2160 |
| The same plus `wl_output` scale 2 and a rewritten physical geometry | events delivered | unchanged 3840 × 2160 |
| `Window::moveResize()` to 1920 × 1080 | frame geometry became 1920 × 1080 while still fullscreen | still a 3840 × 2160 buffer, now with a 1920 × 1080 viewport destination |

The last row is worse than doing nothing: the game kept rendering at 4K and let
its own viewport shrink the finished image. That is the case the handbook
already refuses to call a resolution reduction. None of these levers reduced
rendering work, which confirms for a real game what the earlier cooperative
test client could not: a client that does not act on scale hints cannot be
persuaded after it has chosen its fullscreen size.

#### The one moment that works: before the client enumerates displays

`OutputInterface::bound()` is emitted when a client binds `wl_output`, before it
has a window and before it enumerates modes. At that point
`ClientConnection::executablePath()` names the program, and
`OutputInterface::clientResources()` reaches only that client's resources.
Sending that one client a different current and preferred mode changes what it
believes the screen is, and nothing else in the session observes a change.

| Case | Told the client | Committed buffer | Destination |
| --- | --- | --- | --- |
| SuperTuxKart, OpenGL renderer | 1920 × 1080 | 1920 × 1080 | 3840 × 2160, fullscreen |
| SuperTuxKart, OpenGL renderer | 2560 × 1440 | 2560 × 1440 | 3840 × 2160, fullscreen |
| SuperTuxKart, OpenGL, desktop scale 3 | 1920 × 1080 | 1920 × 1080 | 1280 × 720 logical, the whole 3840 × 2160 output |
| SuperTuxKart, OpenGL, desktop scale 3 | 2954 × 1662 | 2954 × 1662 | 1280 × 720 logical, the whole output |
| SuperTuxKart, Vulkan renderer | 1920 × 1080 | 3840 × 2160 | unchanged |
| Extreme Tux Racer through Xwayland | 1920 × 1080 | 3840 × 2160 | unchanged |

In every successful row the window stayed fullscreen, covered the output, had
no child surfaces and sat at the surface origin: the shape this effect already
accepts. The scale-3 rows matter because that is the television's actual
configuration. The fourth row shows that an arbitrary calculated size is
honoured exactly, so the existing preset and percentage model can drive this
directly rather than being limited to standard modes.

The two failures have different causes and different consequences.
SuperTuxKart's Vulkan renderer defaults to `vulkan_fullscreen_desktop`, which
takes the compositor's fullscreen size instead of selecting a mode, so no mode
information reaches the decision. Extreme Tux Racer is an Xwayland client:
Xwayland binds the output while KWin starts, before any effect is loaded, and
it is a single client for every X11 application, so rewriting its output would
change the screen for all of them. Neither is a reason to call the mechanism
unsupported; both are named limitations of it.

#### What this mechanism is, and what it is not

It tells one client that the screen has a different current mode. It does not
change the output, the desktop scale, any other client's display information,
or the user's game settings, and it needs no launch helper and no restart: the
effect is loaded before the user starts the game. Its cost is that the game's
own settings screen will offer resolutions only up to the advertised size,
which is visible inside the game even though nothing else in the session sees
it.

It is also not enforcement. A client that ignores mode information, or that
asks the compositor for its fullscreen size instead, keeps its own resolution,
as the two failing rows show. Status must therefore report the advertised size,
the desired size and the committed buffer as three separate observations.

Remaining acceptance for this mechanism: real-session and television runs with
input, pointer confinement and controllers; output and scale changes while a
game holds an overridden mode; restoration when the profile, the method or the
effect is switched off; a second client of the same program; games that
enumerate outputs more than once; multiple outputs; HDR and VRR. The runs above
establish committed buffer sizes in headless sessions and nothing beyond that.

#### Implemented on 2026-09-18

The mechanism above is now in the effect, as `UpscaleModeOverride`, with the
recognized applications in `UpscaleApplication` and its catalogue. Verified by
running the production plugin, not a probe, in a nested headless KWin 6.3.6
with the real GPU:

- SuperTuxKart, its own configuration set to fullscreen at 3840 × 2160, with
  the effect at its factory defaults: committed a 2560 × 1440 buffer on a
  3840 × 2160 fullscreen destination. The effect's own status reported
  "2560 × 1440 requested from SuperTuxKart as its screen mode", supplied input
  2560 × 1440, destination 3840 × 2160, and classified the buffer as eligible.
- The unit tests `upscale-application` cover catalogue integrity, the observed
  identities, case sensitivity, a changed Extreme Tux Racer version and
  program matching by file name. The full native suite passes, 11 of 11.

That run also produced the first named explanation for a refusal of a
conforming buffer: the frame was not scaled because **the render target is
rotated or flipped**. KWin's DRM backend sets `OutputTransform::FlipY` on the
colour attachment it renders into, and KWin's own effects multiply
`renderTarget.transform()` into their matrices instead of refusing it. This is
the [rendering slice's](slice-fsr1-hdr-vrr.md#the-scaler-effective-gate)
scaler-effective gate, recorded here because this package's work is what
exposed it: obtaining the smaller buffer is no longer what blocks a scaled
frame.

Not done, and required before this counts as accepted: the real session and
the television, input and pointer behaviour in a game running at a reduced
mode, output and scale changes while an override is in force, a second client
of the same program, restoration paths, HDR and VRR.

#### Tested against a compositor on 2026-09-19

The mechanism had no automated test: `modeoverride.cpp` was 35.1% of lines,
which was the whole of its construction and none of what it does. The nested
KWin session `upscale-integration` now covers it, and the test client binds
`wl_output` so that the assertions are about the events the compositor sent
rather than about what the effect says it sent. A client is the only thing that
can observe this, and it is what a game reads.

Covered on a 128 × 128 screen: nothing is said while the request is off; a
client binding the output afterwards is told 85 × 85 at Quality; the screen's
own mode comes back to a client that was told otherwise once the request is
switched off, which is the restoration path; unlisted applications are asked
nothing until the user turns them on; a catalogue entry reaches a client
through its program name alone, which is the only identity that exists before
it has a window; the entry's own resolution applies while the global preset is
Automatic and an explicit global choice wins over it; a method of `None`
recognizes the application and asks it for nothing; Native asks for the size
the screen already has, so nothing is said; and a scale-driven method on an
unscaled screen says nothing, because such a screen offers that kind of client
no whole step below one. One case goes the whole way: the client was told
85 × 85, committed 64 × 64 instead, and the effect reported both, which is the
distinction between a request and a result that this package rests on.

`modeoverride.cpp` is now 87.6% of lines. What is left is an output unplugged
while an override is in force and a client that exits before restoration, both
of which need hardware or a second compositor process, and both of which are
already in the acceptance list above.

Two defects in the request itself came out of reviewing it beside those tests:

- The resources were given KWin's own answer back only when the effect was
  switched off. A changed preset, a changed percentage or an edited application
  list all move what would be asked for, and the resources kept carrying the
  old mode; anything reading them again would have seen a request this effect
  no longer makes. Every reconfiguration now restores them. What a program was
  told is kept separately and outlives that, because it stays true and is what
  explains the size that program is still rendering.
- A request naming a scale was recorded, and its mode half-sent, to a client
  that bound `wl_output` before version 2, which has no scale event. That
  leaves exactly the disagreement between size and scale these methods exist to
  avoid, and reported a request that was never made. Such a client is now left
  alone. This is not covered by a test: the virtual output in the nested
  session has no scale above one, so no advertisement reaches the scale path
  there at all. It belongs with the output and scale acceptance already listed
  above.

Measuring the frames also had a defect the statistics view would have shown:
the slow-tail figure was written `1%% low`, which is printf's escape and not
KLocalizedString's, so the doubled sign would have reached the screen. Found by
asserting on the text rather than on its presence, and corrected. Two more came
from the same reading: a rejected timestamp, repeated or backward, became the
baseline for the next real presentation and inflated its interval; and the
settings page decided whether anything had been measured from the presentation
mode rather than from the rate, so a window change, which restarts the sampling
without clearing the mode, made it report a rate of minus one per second. The
developer view also claimed variable refresh was unobserved, which stopped
being true when the presentation mode became a measurement; it now reports the
frames and the mode the screen presented them in.

### Per-application X11 buffer control investigation, 2026-09-19

Start state: the effect can advertise a mode to selected native Wayland
clients, but its Xwayland application profile makes no resolution request.
Earlier probes did not establish smaller fullscreen X11 buffers on KWin
6.3.6. The current investigation traces the exact Xwayland, KWin, Extreme Tux
Racer and toolkit code before selecting another mechanism.

End state: a source-backed assessment identifies which mechanisms can obtain
the requested original buffer size from the effect, affect only the selected
application, require no patches to other software or user game configuration,
and be enabled by installing a Debian package. A request, a smaller X drawable,
the game's rendering dimensions and the buffer received by KWin are separate
observations. Unsupported enforcement must be stated explicitly.

Scope: inspect the supported versions and current upstream where relevant;
use isolated container probes for promising paths. Exclusions: changes to the
real desktop, user game settings, installed applications, and production
implementation before feasibility is established. Source copies and probes
belong under `build/x11-resolution-research/`.

Dependencies: Xwayland's client isolation and presentation implementation,
KWin's exported window/geometry APIs, the application's resize handling and
the effect's existing input and eligibility contracts. This is further
investigation within the resolution slice; it does not close the supported
scope or full-acceptance gates above.

Planned acceptance: trace the actual game render-size decisions, distinguish
live control from launch-time helpers, evaluate all five constraints, and
record any observed probe results separately from source-derived predictions.
Run the maintained documentation checks for resulting documentation changes.

#### Source findings

Inspected Xwayland 24.1.6 (also compared its current master output code), KWin
6.3.6 and current master, Extreme Tux Racer 0.8.4, SFML 2.6.2, SDL2's X11
event handler, glmark2 2023.01 and Gamescope at
`c50ddfa9b71a75ec8df94bda8cf31d425dbdda24`. Source copies are in the ignored
research directory. Lasting explanations and source links are in the
[X11 resize section](../upscaling.md#per-window-x11-resize-and-fullscreen-emulation)
and [borderless/Gamescope section](../upscaling.md#borderless-windows-and-gamescopes-approach).

- `xwl_randr_crtc_set()` uses `GetCurrentClient()`. Neither the effect's X
  connection nor a separate `xrandr` process can select a mode for the game's
  connection. `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` is server-produced state,
  not an instruction to set the game's internal emulated mode.
- `xwl_window_should_enable_viewport()` tests the actual application window's
  size and output origin against the owning connection's emulated mode.
  Matching sizes enable the smaller-buffer/full-output viewport.
- Tux Racer's `sf::Event::Resized` handler updates `Winsys.resolution` and
  recreates the window. SFML then selects the fullscreen mode on that game's
  own connection. The effect can trigger this with a standard X window
  resize; there is no game-specific function call or game-config edit.
- SFML rejects sizes absent from its fullscreen mode list. SDL delivers
  configure notifications as resize events, but the application decides how
  to update rendering. glmark2's X11 event loop does not handle resize events.
  Consequently the mechanism is reusable but not universal enforcement.
- KWin 6.3.6's `X11Window::configure()` sizes the native X hierarchy to the
  full output. Current KWin reads Xwayland's emulation property and configures
  the client at the emulated size while keeping fullscreen geometry.
- Gamescope creates private Xwayland servers and headless outputs at its
  nested render dimensions, sets the game's `DISPLAY`, and uses ordinary
  `XResizeWindow()` for fullscreen sizing. Its compositor keeps committed
  texture dimensions separate from window geometry and explicitly handles
  borderless games retaining larger swapchains. Its Vulkan WSI layer can
  expose X extents and report out-of-date swapchains, but is application-side
  integration, not a plugin API for existing arbitrary renderers.

#### Observed prototype results

Environment: disposable container based on the maintained Trixie image,
unmodified KWin `4:6.3.6-1`, Xwayland `2:24.1.6-1`, Tux Racer `0.8.4-1`,
SFML `2.6.2+dfsg-2+b1`, glmark2 `2023.01+dfsg-2`, and Mesa llvmpipe
`25.0.7-2+deb13u1`. Each run used a private D-Bus session, home/config/runtime
directories and a 3840 × 2160 KWin virtual output at scale 1. KWin used
QPainter and Xwayland's software path; these are real X11/OpenGL application
and Wayland buffer observations, not GPU, FSR, performance or TV acceptance.

The temporary `x11probe` effect reads the target identity and Width/Height
from its own configuration. It observes the X geometry, KWin fullscreen and
frame geometry, surface buffer/destination sizes and emulated-mode property.
The selected application starts normally; after six seconds the effect acts
on that window. glmark2 starts later while Tux Racer remains open. No changes
were made to game settings, KWin, Xwayland or the production effect.

| Probe operation | Target result | Unrelated fullscreen X11 app |
| --- | --- | --- |
| Observe default Tux Racer | 3840 × 2160 buffer and destination | glmark2: 3840 × 2160 |
| `Window::moveResize()` to 1080p | Game recreates its window; KWin returns it to 4K. Repeating causes window recreation, not stable reduction. | glmark2: 3840 × 2160 |
| Raw XCB resize of target frame/wrapper/client to 1080p | Same recreation/reset failure. | glmark2: 3840 × 2160 |
| Targeted fullscreen event filter plus XCB geometry, 1080p | Stable 1920 × 1080 buffer; fullscreen frame and viewport destination remain 3840 × 2160. | glmark2: 3840 × 2160 while target stays at 1080p |
| Same filter, 1440p | Stable 2560 × 1440 buffer; fullscreen frame and destination remain 3840 × 2160. | glmark2: 3840 × 2160 while target stays at 1440p |
| Select glmark2 itself for the same 1080p operation | X drawable and received buffer become 1920 × 1080, but destination is also 1920 × 1080 and no emulated mode exists. Its own renderer still reports 4K; this is not accepted rendering reduction. | Tux Racer stays at 3840 × 2160 |
| Borderless `setNoBorder()` and `moveResize()` on glxgears | Stable 1920 × 1080 original buffer and render viewport in a borderless 1920 × 1080 window. No fullscreen enlargement or RandR emulation was claimed. | Later fullscreen glmark2: 3840 × 2160 buffer and viewport |

The filter handles the selected window's EWMH fullscreen request through
`X11EventFilter`. It calls `X11Window::setFullScreen()` while native geometry
updates are blocked, unblocks without flushing the full-size configure, and
configures the X frame, wrapper and client itself. It also intercepts that
window's later configure requests. Tux Racer's ordinary resize handling then
produces its own RandR request, and Xwayland's existing viewport takes over.
The generic implementation contains only configurable identity matching and
window-system operations. The prototype is deliberately incomplete: it does
not yet reconcile all cached geometry, EWMH state combinations or restoration.

Successful run directories include `run-filter-1920x1080-1789810444`,
`run-filter-2560x1440-1789810515` and
`run-filter-1920x1080-1789810649` under the research directory. In the last,
apitrace independently observed real Tux Racer `glViewport` calls changing
from 3840 × 2160 to 1920 × 1080, followed by 1,267 swaps after the last smaller
viewport call. The same run recorded glmark2's real viewport calls at
3840 × 2160 and 2,413 swaps. Reconstructed initial-state calls marked `fake`
were excluded from the viewport evidence. The inspected Tux Racer trace had
no framebuffer binds, framebuffer blits or renderbuffer-storage calls, and no
4K render-size texture allocations among its `glTexImage2D` calls. This
supports original smaller rendering in the observed menu sequence, not every
scene or internal target in every application.

The borderless run `run-borderless-1920x1080-1789810864` selected glxgears by
its observed caption. It recorded a real 1920 × 1080 viewport call followed
by 14,424 swaps; later fullscreen glmark2 retained real 3840 × 2160 viewport
calls and buffers. Before removing its decoration the initially output-sized
glxgears window had a 3840 × 2135 content area. This proves cooperative
ordinary-window resizing with a second X11 implementation, but is not a
second game, a completed output-filling presentation path or an input test.

The negative glmark2 case was repeated with caption matching and apitrace in
`run-filter-1920x1080-1789810955`. Its actual viewport remained 3840 × 2160
while the X drawable and KWin buffer were 1920 × 1080. This is a concrete
counterexample to treating the received buffer size alone as proof of correct
lower-resolution rendering. The test scene was a clear, so it establishes the
viewport mismatch rather than a visual judgment of cropped game content.

Instrumentation/setup limits: initial runs with incomplete X11 socket/plugin
setup are excluded. A simple GL symbol observer produced no measurements;
SFML resolves GL through `dlopen`, and apitrace's library redirection was used
instead. Both traced applications presented throughout the observation but
apitrace's signal handler faulted during the harness's forced termination.
Untraced runs ended on the harness's SIGTERM. These are not clean-exit or
effect-unload acceptance results. An initial borderless probe did not match
glxgears because that application did not set WM_CLASS; the successful run
used the actual caption instead. These identity choices are probe configuration,
not special cases in the resize mechanism.

#### Assessment against the requested constraints

The per-window resize/filter path is feasible inside an effect, and the
observed positive runs isolate it to the selected application without any
patches or user game configuration. It uses APIs already shipped with KWin
6.3.6. The follow-up below demonstrates installation and automatic loading of
a research Debian package; the production package is unchanged. Its confirmed
fullscreen compatibility class is a client which responds
to resize and selects its own emulated fullscreen mode. Broader toolkit/game
coverage includes the SDL borderless case below; the glmark2 counterexample
prevents a universal claim.

Borderless window resizing is an additional candidate requested by Jens. It
can avoid fullscreen mode selection for clients which follow ordinary window
resizes. Enlarged presentation and input mapping belong with the
[geometry slice](slice-scaling-geometry.md); obtaining and validating the
original smaller buffer remains owned here. The production effect currently
requires fullscreen coverage and cannot accept that path without integration.

Documentation validation: `python3 -B tools/run-checks.py docs --base HEAD`
passed both configured pre-commit stages in the maintained Trixie container.
The first attempt failed because Ruff tried to use an unwritable cache outside
`build/`; setting `RUFF_CACHE_DIR` to the research directory's cache fixed the
check environment. These were documentation-only checks. Production builds,
the full regression suite, production-package checks, neon and hardware tests
were not run for this investigation. The separate research-package check is
recorded below.

Remaining: integrate a selected method with production settings and truthful
result reporting; test
event/cache ownership and restoration, output/scale changes, input and
confinement, focus and replacement windows, unsupported modes and clients,
both supported KWin versions, package installation, and hardware acceptance.

Follow-up feasibility checks: package the temporary effect as a local research
Debian package and load it through KWin's normal installed-plugin path. Repeat
the target/other-client test, then exercise configuration changes and disabling
the method. Broaden the ordinary-window resize probe to another game/toolkit
where available. These checks establish delivery and lifecycle feasibility;
they do not substitute for production integration or close this slice's wider
release and hardware acceptance criteria.

#### Installed-package and lifecycle results

The follow-up used another disposable container from the same maintained
Trixie image. The temporary effect was packaged as
`kwin-effect-upscale-x11-probe_0.0.1+research_amd64.deb`, with its library in
the distribution's Qt 6 KWin plugin directory and target/size defaults in
`/etc/xdg/x11probe.ini`. `dpkg --install` succeeded. A new isolated KWin
session loaded the effect from that installed path, without `QT_PLUGIN_PATH`
and without a per-user effect-enable entry or initial probe configuration.
The metadata's enabled-by-default setting and packaged defaults sufficed.
This is a local research package, not a release or the production package.

`run-installed-1789811381` under the research directory contains the completed
run and traces. `run-installed.py` writes only the effect's configuration when
changing requests; neither game receives resolution arguments or game-setting
edits. Tux Racer starts normally; fullscreen glmark2 starts twelve seconds
later and remains open throughout subsequent transitions.

| Phase | Tux Racer buffer and real GL viewport | Completed swaps in viewport phase | glmark2 |
| --- | --- | --- | --- |
| Initial default | 3840 × 2160 | 279 | Not started yet |
| Installed effect's default request | 1920 × 1080 | 834 | Starts at 3840 × 2160 |
| Change effect request | 2560 × 1440 | 474 | 3840 × 2160 |
| Disable method | 3840 × 2160 | 454 | 3840 × 2160 |
| Re-enable at 1080p | 1920 × 1080 | 415 | 3840 × 2160 |
| Unload effect, then reload in observe mode | 3840 × 2160 | 592 | 3840 × 2160 |

Tux Racer's logical fullscreen frame and surface destination remained
3840 × 2160. glmark2's viewport stayed at 3840 × 2160 across 7,231 completed
swaps, and every recorded glmark2 buffer/destination had that size. An
independent RandR query still reported a 3840 × 2160 current screen.
The harness closed both windows through their normal window-manager close
requests; both processes exited with status zero and the traces contain no
termination-handler faults. This supersedes the earlier forced-termination
limitation for this particular fullscreen lifecycle sequence.

The prototype records affected windows with guarded pointers. Disabling or
unloading stops its interception and restores native fullscreen dimensions;
the game then recreates its normal-resolution window. This establishes one
working restoration sequence, not complete ownership across monitor changes,
all EWMH state combinations or other clients. The protocol still depends on
application cooperation and the production integration remains open.

`verify-installed.py` checks the expected six viewport phases, continuing
swaps in each phase, repeated target buffer observations, all unrelated-client
samples, restoration after unload/reload and clean exits. It passed against
these completed logs. `dpkg --verify kwin-wayland kwin-common xwayland
extremetuxracer` produced no discrepancies; `cmp` confirmed that the installed
probe library matched the tested build. As in the earlier container setup,
the container's KWin file capability was removed to permit running the
isolated session; this is a container-execution accommodation, not part of the
resolution-control method or Debian package.

#### Independent SDL game and research conclusion

The additional game is SuperTux `0.6.3-3` using SDL `2.32.4+dfsg-1`, explicitly
running through SDL's X11 backend. It starts with its default window and no
resolution arguments. The same generic borderless operations first request
3840 × 2160, then 1920 × 1080 through effect configuration. No application
code or settings are edited. `run-borderless-3840x2160-1789811448` records
both corresponding received buffer sizes. The real default-framebuffer
viewport follows the two sizes, with 286 completed swaps at 4K and 754 at
1080p. Later fullscreen glmark2 retains a 4K buffer and viewport across 1,827
completed swaps. This run used the original forced-stop harness, so its
tracer termination faults are not clean-exit evidence.

The trace also shows SuperTux rendering an intermediate framebuffer at
1368 × 769 in both phases. Consequently this verifies the requested window
buffer and presentation viewport; it does not establish a proportional
reduction in the game's internal rendering work. Source inspection confirms
that its SDL resize handler updates `window_size` and reapplies video
configuration. Borderless fullscreen enlargement and input remain separate
integration work, as described above.

The requested research outcome is established: at least one mechanism can be
implemented in a KWin effect, selectively obtains the configured Tux Racer
buffer, preserves the unrelated fullscreen application's normal resolution,
requires neither external patches nor user game reconfiguration, and loads
from a Debian-installed plugin. Standard targeted resizing is also reusable
with a different toolkit and game in borderless mode. This conclusion closes
the feasibility question for cooperating clients; it neither claims universal
forcing nor closes this slice's production, compatibility and hardware gates.

The handbook now preserves the exported API sequence, the assessment against
all five constraints, borderless and Gamescope boundaries, and the required
lifecycle test sequence. Earlier statements that Xwayland cannot be addressed
separately have been narrowed to the production Wayland mode-advertising path.
The prototype's observed results remain here; they do not change the production
catalogue's `None` method for Tux Racer.

## Remaining work

### A game that never exits

Asked for by Jens on 2026-09-19. A game can disappear without warning —
`kill -9`, a driver fault, a crash inside the engine — and everything this
slice arranges for it has to survive that: the advertised mode a client was
told, the X11 window size that was requested, the per-window bookkeeping, and
the resources KWin holds on the effect's behalf. Nothing may be kept for a
client that cannot receive it, and nothing may grow across repeated launches.

Announcements whose client is gone are dropped now, so restoring the real mode
walks only clients that still exist. What remains to be tested: a client killed
between the request and its first commit, a client killed while its window is
being resized on X11, and repeated kill-and-relaunch cycles with process and
video memory watched across them. The display's own side of this is in the
[development infrastructure slice](slice-development-infrastructure.md).

### Production X11 integration

Jens requested integration of the demonstrated mechanism into the actual effect
so that Tux Racer works without game configuration. Start state: the production
catalogue still gives Tux Racer no method; the proven controller exists only
in the ignored research build. End state for this implementation: a generic
profile-selectable X11 resize method, a shipped Tux Racer profile, truthful
requested/observed status, bounded negotiation and restoration on disable or
unload, with an automated X11 regression and the real-game isolation sequence.
Include selected borderless X11 and Wayland windows whose content exactly
covers one output. Preserve logical presentation and input mapping; a smaller
native window alone is insufficient. Arbitrary internal render targets and
universal game compatibility remain outside this implementation. No external
software patches or user game-setting changes are permitted.

Approach: separate X11 negotiation and native geometry ownership from rendering;
reuse existing profile matching and size calculation, restrict interception to
selected clients, preserve unrelated EWMH operations, and handle replacement
windows and output changes. Add the method to configuration and diagnostics,
exercise refusal and restoration as well as success, and verify the actual
production controller with Tux Racer plus an unrelated fullscreen X11 client.
The supported-scope gate requires both maintained compiler/container builds,
configured checks, and virtual-backend runtime coverage. Physical input,
GPU import, HDR/VRR and real-TV acceptance remain the full-acceptance gate.

Multi-output steering: use the selected window's output geometry and scale,
never the global active output. Test target and unrelated fullscreen clients
on separate outputs as well as on the same output. Xwayland's emulation is
per client connection and per server output, not a server per display. SFML
2.6.2 selects the primary RandR output when recreating a fullscreen window;
secondary-output and move cases must be measured and refused/restored when
the client's own mode selection does not match the target output.

Implementation progress:

- Added profile method `X11Resize`, separate requested/observed diagnostics,
  event interception and restoration, and the shipped Tux Racer profile. The
  controller uses exported KWin interfaces and XCB geometry operations.
- Added profiled full-output borderless eligibility for native Wayland and
  X11. The supplied surface must retain the whole logical destination. X11
  clients without matching emulation are restored after bounded negotiation.
- The maintained Trixie GCC build passed all 16 CTest entries. The X11 fixture
  uses separate connections and two 4K virtual outputs; fullscreen and
  borderless lifecycle cases passed on both outputs, alongside an unrelated
  fullscreen client. The native Wayland borderless case passed as well.
- Initial production-controller Tux Racer runs observed 1080p and 1440p
  supplied buffers with a 4K destination, normal restoration and an unrelated
  fullscreen glmark2 at 4K. These use the actual effect through the maintained
  test driver, which substitutes capture rendering for KWin's QPainter virtual
  backend. They do not validate physical GPU import or input.
- A traced startup found Tux Racer can discard a resize while changing game
  states, even after submitting a buffer. Initial requests wait for a supplied
  buffer; one failed negotiation restores normal geometry before retrying.
  Replacement windows already carrying the requested emulation remain eligible
  for early interception. A separate regression drops the first resize and
  confirms recovery; the non-cooperative fixture confirms bounded refusal.
- A two-output real-game run confirmed SFML moved Tux Racer from the secondary
  output to primary when recreating its window. Added the generic profile
  constraint `X11PrimaryOutputOnly`, enabled for Tux Racer, to refuse that
  request before touching geometry. Real-game rerun
  `run-production-secondary-1789814123` retained Tux Racer's original XID,
  secondary-output position and normal 4K buffer throughout. The additional
  regression passed; the generic fixture supports either output.
- Final traced production run `run-production-1789814425` passed the independent
  verifier. Actual Tux Racer viewport/swap phases were 4K/1, 1080p/171, 4K/14,
  1080p/868, 4K/2, 1440p/465, 4K/462, 1080p/475 and 4K/585. Short restoration
  and recreation phases remain in the observation, including the startup
  retry. Settled reduced buffers had a 4K destination and matching emulation.
  The unrelated fullscreen glmark2 kept a 4K viewport for all 7274 swaps and
  a 4K supplied buffer throughout. Both applications closed normally with
  status 0; no reconstructed trace state was counted as a rendering call.
- GCC and Clang builds succeeded on both maintained environments, warnings as
  errors. Trixie passed all 16 CTest entries and neon passed all 11 available
  entries. The latest X11 regression passed its fullscreen/borderless cases,
  both output positions, unavailable-mode refusal, primary-only restriction,
  bounded retry and disable/unload/reload restoration. Neon does not currently
  build the repository's QPainter integration driver, so these X11 runtime
  results remain minimum-version results, not a modern-KWin runtime claim.
- Both pre-commit stages passed; explicit new-file and final documentation
  checks supplement the tracked-file run. Plugin metadata passed its schema
  check. Static analysis passed all production translation units: the first
  pass crashed in LLVM's include sorter for `upscale.cpp`, whose stable-file
  rerun passed without changing the enabled checks.
- A Trixie amd64 snapshot package built through `debian/rules`, passed all 16
  package-build tests and Lintian, and passed installation, reinstallation,
  installed effect/settings factory loading, removal and purge in a fresh
  disposable container. Artifacts are under `build/x11-implementation-package/`.
  This is a local test package; no release or host installation was performed.

### Independent output policy and pixel threshold

Starting state: X11 requests belong to a window and output, but render
selection still rejects simultaneous candidates across different outputs.
There is no minimum output resolution, and a global preset can override a
profile's Native choice. The owner requires secondary outputs to participate
independently and clarified that the threshold compares total physical pixels,
not separate dimensions.

End state: each output independently selects its eligible fullscreen or
profiled borderless window. A global minimum pixel count defaults to Full HD
(2,073,600), with a per-application override. Equality and smaller outputs
bypass both requests and FSR; zero disables the threshold. Native application
rules explicitly bypass requests and rendering even with a global preset.
Existing restrictions on unsupported client behavior remain separate.

Scope: shared policy, settings, negotiation, per-output selection and regression
coverage. Exclusions: arbitrary internal game render targets, new launch
mechanisms, rotated-output support and closing physical acceptance by inference.
Dependencies: the controllers and borderless eligibility above. Supported-scope
gate: pixel boundary/ultrawide tests, settings persistence, Wayland policy tests,
two-output X11 isolation and simultaneous scaling, both maintained builds and
required checks. Full acceptance still includes real multi-output GPU rendering,
input and output movement on wzpc. Planned checks are not yet results.

Observed during implementation:

- Added shared pixel-count comparison with widened multiplication, global and
  per-profile settings, Native opt-out precedence and output-scoped selection.
  Requested Wayland sizes and rendered-buffer records also retain output/window
  identity instead of letting another display replace their diagnostics.
- Initial Trixie GCC passed all 16 tests, including threshold inheritance,
  equality, per-profile override and Native behavior for native Wayland. The
  X11 case exercises Native and threshold bypass on the second output while
  the first stays scaled, then observes scaler captures for both outputs.
- The first Clang run caught an existing fixture assumption that RandR monitor
  indices followed horizontal position. KWin 6.3.6 resolves EWMH fullscreen
  indices through `xcb_randr_get_monitors`; the fixture now resolves its intended
  origin through that list too. The corrected GCC suite passed all 16 tests.
- Both updated neon builds passed their 11 available tests. The corrected
  Trixie Clang suite also passed all 16 tests. Both pre-commit stages passed,
  including explicit checks of the new files. Static analysis found the added
  refusal messages pushed their switch beyond the function-size limit; those
  messages were split into a separate helper for the final focused rerun.
  That rerun passed static analysis; all four compiler/environment builds and
  their focused resolution/snapshot tests passed after the split as well.
- Real-game trace `run-production-1789815621` passed the independent verifier.
  Tux Racer's actual viewport/swap phases were 4K/1, 1080p/177, 4K/13,
  1080p/858, 4K/2, 1440p/464, 4K/452, 1080p/475 and 4K/588. Settled reduced
  buffers covered the 4K destination. The unrelated fullscreen glmark2 kept
  a 4K viewport for all 6694 swaps and a 4K supplied buffer throughout; both
  applications exited normally. This run used the new default Full HD
  threshold and retains the startup retry in the observation.
- The updated test package under `build/pixel-policy-package/` passed all 16
  package-build tests, installation/reinstallation, installed factory loading,
  removal and purge. Metadata validation passed. Lintian returned success with
  two long-snapshot-filename warnings for the `.changes` and `.buildinfo` files.
  The package contains the complete policy; it precedes the behavior-preserving
  split of the refusal-message helper. No host install or release was performed.
- Physical acceptance remains open: mixed-resolution/scale outputs, output
  movement, actual GPU import and input must still be exercised on wzpc.

- [x] Establish what a loaded effect can do to a game the user started, and
      implement the one mechanism that was observed to work.
- [ ] Accept it in the real session and on the television: input, pointer
      confinement, output and scale changes, restoration, HDR and VRR.
- [ ] Resolve the recorded compatibility and input gaps on both KWin targets,
      including Xwayland and the fullscreen-desktop clients this cannot reach.
- [ ] Complete the acceptance criteria and document supported limitations.
