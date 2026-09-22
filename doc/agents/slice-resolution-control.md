<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: obtaining smaller game buffers

## Current status and scope decision

The selected route is cooperative control entirely from the plugin. The owner’s
2026-09-19 requirements exclude launch helpers, relaunching and changes to shared
output information, even where earlier prototypes worked. Those dated experiments
below are retained as research, not implementation tasks or user setup guidance.

Production Wayland mode/scale advertisement and targeted X11 resize are implemented.
The X11 path has obtained smaller Tux Racer buffers with restoration while an
unrelated fullscreen X11 client retained 4K. Full-output borderless eligibility
and per-application physical-pixel thresholds work independently per output.
The production integration and pixel-policy sections below record actual tests.
Tux Racer’s SFML primary-output restriction is a client-specific limit; it does
not restrict all scaling to the primary display.

Full physical-display input, image quality, performance, HDR/VRR and broader
Wine/Proton compatibility remain open. The earlier rendering refusal is fixed;
nested GPU processing is observed, but it does not close these acceptance gates.

## Historical start state

At the start, the effect scaled supplied buffers and showed a desired
resolution, but sent no production resolution requests. Experiments below
establish several partial
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
implemented native Wayland and X11 methods provide bounded candidate paths.
Closing this gate requires their ownership and restoration to be settled across
output changes and effect lifecycle, and physical-display acceptance for the
supported client class.

**Full acceptance.** The criteria below, unchanged: Xwayland on the minimum
supported version, Valve Proton and standalone Wine across the required
graphics paths, input, HDR, VRR and real-game hardware acceptance.

The cooperative X11 path addresses a bounded class on KWin 6.3.6; it does not
establish all Xwayland, Proton or Wine compatibility. Those stay required,
keep this document open, and
must be named as unsupported in the release that ships without them.

## Scope and boundaries

Own cooperative in-session negotiation, original-buffer delivery, input
mapping and restoration. Private displays and launch helpers are excluded.
Implement the verified method capabilities and result reporting
needed by callers. Keep KWin unpatched and retain the 6.3.6 target.

Application matching belongs to [profiles](slice-application-profiles.md).
The earlier launching proposal is superseded; see
[application launch configuration](../upscaling.md#application-launch-configuration-and-method-discovery).
Image quality and filter performance belong to [rendering](slice-fsr1-hdr-vrr.md).
Geometry for different aspect ratios and windowed-game presentation are outside
this package. Their absence does not justify stretching or changing the output.

## Dependencies

Use the existing supplied-buffer scaler and explicit selection of the target
application. Integrate with the profile identity contract when available.
Publish method capability and result state for settings and diagnostics: live
versus bind-time requests, actual response and restoration. Complete end-to-end
cases use the integrated profiles and normally started applications.

## Approach

1. Resolve the minimum-version Xwayland issue and native scale-policy ownership
   using the recorded experiments; select only verified routes.
2. Implement method capability checks and original-buffer forwarding without
   downsampling a completed native-size image.
3. Verify input, surface trees, fullscreen transitions, output/desktop scaling,
   cleanup, HDR and VRR for each supported method.
4. Integrate method selection and truthful target/actual state with the shared
   configuration contract. Automatic follows the selected profile’s preset;
   without a profile target it makes no request.
5. Complete the handbook's game-control acceptance; retain unsupported cases
   and measured limitations in human documentation and implementation comments.

## Acceptance criteria

Planned checks, not observed results:

- Apply both [isolation and compatibility gates](../upscaling.md#isolation-and-compatibility-acceptance)
  to every claimed application/runtime combination. Keep supported, limited,
  unsupported and untested evidence distinct; Wine and Proton remain required.
  Include simultaneous unrelated X11/Wayland clients, independently ruled
  outputs, both launch orders, lifecycle failures and restoration. Measure the
  session-wide scanout consequence separately from resolution/state isolation.
- Cooperative, ignored and adjusted live requests, ownership and restoration;
  Automatic without a profile target makes no request; explicit methods do not
  silently fall back.
- Original 1920 × 1080 and 2560 × 1440 buffers reach a 3840 × 2160 destination
  without changing the physical output or unrelated applications. Cover native
  OpenGL/Vulkan and the required Wine/Proton graphics paths.
- Follow the handbook's [game-control tests](../upscaling.md#game-identification-and-resolution-control-tests)
  with Extreme Tux Racer and SuperTuxKart, then Steam/Epic cases in the required
  order. Manual in-game reduction is a baseline, not proof of effect control.
- Cover live target changes, pending launch, unsupported negotiation, fixed-size
  clients, unsupported child surfaces, fullscreen modes, fractional desktop scales,
  multiple outputs, pointer mapping, confinement, locking and controllers.
- Verify HDR and actual adaptive presentation on hardware through each claimed
  method, and cleanup after failure, game exit, output changes or deactivation.
- Run repository checks, both compiler/container builds with warnings as errors,
  virtual-backend integration and real-game/TV acceptance. Do not reuse a probe
  build as evidence for the production effect.

## Findings and observed results

The dated observations below were retained from the original combined slice.
Their stated environments and limits still apply; moving them is not a new run.
Later plugin-only requirements supersede the early launch-helper direction,
method-discovery plans and claims that production requests are unimplemented.

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
[rendering slice](slice-fsr1-hdr-vrr.md#aspect-ratio-and-integer-scaling); obtaining and validating the
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

### Left 4 Dead 2 against X11Resize, 2026-09-19

**Concluded: this client cannot use the method, and the reason is not its
refusal to resize.** The measurements below were taken before that was
established; the conclusion is stated here first because it is what the entry
and the handbook have to carry.

Observed on pcjensd, KWin 6.3.6 Wayland, one output DP-3 at 3840 x 2160 scale
1.45, RTX 5090 on NVIDIA 615.71.09. Left 4 Dead 2, Steam build 23990068, the
native Linux Source build, launched normally from Steam. Its stored settings
were read from `left4dead2/cfg/video.txt` rather than from the menu:
`setting.fullscreen` `1` and `setting.nowindowborder` `0`, so the game was in
exclusive fullscreen and not borderless, and `setting.defaultres` 2560 with
`setting.defaultresheight` 1440 against a 3840 x 2160 output.

That combination is the only one that tests anything. A game asking for the
mode the output already has gives Xwayland nothing to emulate, and an absent
emulation then means nothing at all. Two earlier runs were inconclusive for
exactly that reason and were read as evidence before the configuration was
checked.

With a genuine mode change to make, sampled once a second:

    20:49:59  win=3840x2160  EMU=ABSENT
    20:50:00  win=2560x1440  EMU=ABSENT   the game applied its own setting
    20:50:01  win=3840x2160  EMU=ABSENT   forced back within one second

`_XWAYLAND_RANDR_EMU_MONITOR_RECTS` was absent at every sample of every run:
during startup, at the game's own non-native resolution, under our request, and
in steady gameplay. **Source sets its window size and never asks RandR for a
mode**, so Xwayland establishes no per-client emulation, KWin's fullscreen
handling returns the window to the output size, and the game renders the
output's resolution whatever its own setting says.

Three consequences, all of them measured rather than reasoned:

- `X11Resize` cannot work for this client. It accepts the resize immediately -
  asked for 2560 x 1440 it supplied exactly that buffer and held it for the
  full three seconds - but without the emulation the window stops covering the
  output, which is a failed trial under this project's own rule whatever the
  buffer says.
- The user configuring the game themselves does not work either, so the
  `UserConfigured` method specified in the handbook does not rescue this title.
  The same forcing defeats the game's own menu.
- The game therefore renders at output resolution on a 4K screen no matter what
  is selected, which is a performance problem belonging to the game and the
  window manager, not to this effect. `scaling=0` throughout every observation.

This is a property of the engine, not of the title: every native Source game
shares the `hl2_linux` identity and this behaviour. Whether other SDL2 clients
behave the same way is not established here.

**What would be needed to reach such a client** is for the compositor to accept
a window smaller than its output as the fullscreen presentation, scale it up
itself, and transform input to match. The emulation currently supplies all
three for free. Doing without it is KWin work rather than effect work, and the
input mapping is the part that decides whether it is feasible at all; a smaller
buffer whose pointer lands in the wrong place fails the trial rule as surely as
a window that does not cover the screen.

#### The measurements this conclusion was drawn from

Observed on pcjensd: KWin 6.3.6 Wayland session, one output DP-3 at
3840 × 2160 with scale 1.45, RTX 5090 on NVIDIA 615.71.09, effect build
`0.1.0+git20260919.759c8a4993-dirty`. Left 4 Dead 2, Steam build 23990068, the
native Linux Source build (`hl2.sh` → `hl2_linux`), launched normally from
Steam through pressure-vessel. Profile: `WindowClass=hl2_linux`,
`Instance=hl2_linux`, `Method=X11Resize`, `Preset=Quality`, giving a
2560 × 1440 request.

**Identity.** `hl2_linux` for both class and instance, read from the running
window. This is the Source engine binary, not the game: every native Source
title shares it, so the entry cannot be specific to Left 4 Dead 2. The window
title does name the game, but titles are excluded as identity because they
carry save-game and player names.

**The request is followed, once the window has settled.** The attempt made when
the window appeared failed twice and latched. Reapplying settings while the game
was running produced, within two seconds:

    Desired: 2560 × 1440 requested from Left 4 Dead 2 as its X11 window size
    Supplied input: 2560 × 1440
    Destination: 3840 × 2160

So Source does act on an external X11 resize. The startup failure was the
loading-transition discard this slice already describes, and the single retry
in `x11resolution.cpp` was not enough to outlast it. Whatever else is decided,
the method is not inapplicable to this engine.

**It was refused for presentation, not for size.** The refusal on that frame was
`ResizedSurface` — "the window's surface is displayed at a different size than
the window". The 1440p buffer was being presented stretched into the 3840 × 2160
window, so Xwayland was performing the enlargement itself rather than leaving a
1440p window for the effect to scale. Of the two conditions this slice records
for fullscreen presentation — an emulated mode on the client's connection, and
native X geometry matching that mode at the output origin — the second was not
satisfied. The validation in `x11resolution.cpp` therefore rejected the result,
took its one retry, refused, and restored normal geometry; the game returned to
3840 × 2160.

**The restore did not leave the window covering the output.** Sixteen seconds
later the supplied buffer was 993 × 748 and the window was no longer fullscreen.
Under the trial rule an image that stops covering the screen is a failure
outright, so this run is recorded as failed even though the buffer did shrink.

**Not established.** Whether the presentation condition can be met for this
client at all; whether more than one retry, or a request deferred until after
the first map has loaded, changes the outcome; and whether the small window
after restore is a fault in the restore path or the game reacting to it.

**A separate problem, not caused by the effect.** The game produced roughly
18–24 frames per second at 3840 × 2160, dipping to about 3, with a worst frame
of 6.7 seconds. `scaling=0` throughout: the effect never composited a scaled
frame, so it is not in this path. The GPU was at 16% and the X11 GL renderer
resolves to the RTX 5090, so the hardware is present and idle. The cause was not
established — the game being unfocused while the session was driven from another
window, Source's own GL path on this driver, and the container runtime are all
candidates, none tested. Until this is understood, no frame-time comparison from
this machine can support a preset, which under the submission rule means the
entry's preset stays `Automatic`.

#### Why the mode is never set, and who would have to own the scaling

Read on 2026-09-19 from the captured evidence only — no new run against the
game, no tracer attached to it. Sources: the xtrace and strace logs under the
ignored `build/research/traces`, SDL at `release-2.0.14` and
`release-2.32.x` in the research checkout, `xserver-24.1.6/hw/xwayland`, and
KWin 6.3.6 as installed here — its headers under `/usr/include/kwin` and
`libkwin.so.6.3.6`.

**The client does reach SDL's mode-setting code, every single time, and is
turned back one request short of the mode change.** Fifteen fullscreen state
changes appear in the trace — eight `_NET_WM_STATE_FULLSCREEN` adds, seven
removes — and each one is preceded, within a millisecond and with nothing in
between, by exactly `RRGetScreenResources`, `RRGetOutputInfo` and one
`RRGetCrtcInfo`. That triple is the opening of `X11_SetDisplayMode`
(`src/video/x11/SDL_x11modes.c:983-994` at 2.0.14, unchanged in 2.32). Its
fourth request, `RRSetCrtcConfig`, never follows. No other SDL function emits
that triple once per fullscreen call: `X11_InitModes_XRandR` also reads the
primary output and asks for two CRTC infos, and `X11_GetDisplayModes` asks for
one per mode, which is the single burst of thirty-eight at 741.588.

Since SDL 2.0.16 the function returns between those two requests when the CRTC
is already in the wanted mode — commit `25cd749ad`, Simon McVittie, 2021-08-12,
written because setting a redundant mode on Xwayland could disable the CRTC
for good. The traffic stops exactly there. **So the mode SDL asked for is the
mode the output already has, 3840 x 2160.**

The path that got there is exclusive fullscreen, not `FULLSCREEN_DESKTOP`.
Desktop fullscreen calls `SDL_SetDisplayModeForDisplay(display, NULL)`, which
compares the desktop mode against the current one and returns before any X
traffic, so it can never produce the triple. The first triple also arrives four
milliseconds after the thirty-eight-mode enumeration, which is the lazy
enumeration `SDL_GetClosestDisplayModeForDisplay` triggers on that same path.

The game's own resolution in this run was 2560 x 1440, twice over: the D3D9
device is created at that size (`game-stdout.log:68`), and the size hints SDL
writes when it makes the window non-resizable again carry
`min = max = 0xa00 x 0x5a0`, which is SDL's stored windowed size. **The engine
asks for a fullscreen window at the desktop mode and renders its own resolution
into whatever window it gets.** Resolution and display mode are separate
decisions here, and only the window size follows the menu.

Nothing in that decision is reachable from outside the process. The mode comes
from `window->fullscreen_mode`, written only by `SDL_SetWindowDisplayMode`, or,
when that is unset, from the window's windowed size; `GetClosest` then maps it
onto the server's list. All of it is client state. The server's list is not the
obstacle either: Xwayland offered 2560 x 1440 as mode `0x3f`. The one input a
compositor does feed is `_NET_SUPPORTING_WM_CHECK`: without it SDL takes
`X11_BeginWindowFullscreenLegacy` (`SDL_x11window.c:1463-1490`) and covers the
screen with an override-redirect window of its own. That still sets no mode,
and it would mean telling every X client in the session that there is no EWMH
window manager.

Not established: which library the process loaded. strace shows the game
opening `steam-runtime/pinned_libs_32/libSDL2-2.0.so.0` immediately after
`execve` of `hl2_linux`, and the host's `libSDL2-2.0.so.0.3200.4` opened only
by the runtime's `ldconfig`. Both the pinned Steam Runtime build and 2.32.4
contain the same early return, and both reach it only because the requested
mode equals the current one, so the conclusion holds either way.

**What Xwayland's per-client emulation is.** Four separate mechanisms, all
started by the client's own `RRSetCrtcConfig` (or a VidMode switch):

1. `xwl_randr_crtc_set` (`xwayland-output.c:1048`) stores the mode per client
   and output and deliberately does not call `RRCrtcNotify`, so the RandR state
   every other client sees is untouched — the comment at `:1074` says so.
2. `_XWAYLAND_RANDR_EMU_MONITOR_RECTS`, one `x, y, width, height` per emulated
   output, is written on every toplevel that client owns and on later ones as
   they are realized (`xwayland-output.c:437-489`, `xwayland-window.c:1512`).
3. That client alone receives a synthetic `ConfigureNotify` for the root with
   the emulated size, and an `RRScreenChangeNotify` if it asked for one
   (`xwayland-output.c:537-566`). The root's real size never changes; only the
   events lie.
4. The viewport: when the client's toplevel sits exactly at the output origin
   and is exactly the emulated size, Xwayland attaches a `wp_viewport` with
   source the emulated size and destination the output size, and records
   `viewport_scale = emulated / output` (`xwayland-window.c:548-616, 438-467`).

The input adjustment is that same scale applied to everything that comes back
through the enlarged surface: absolute pointer positions
(`xwayland-input.c:674`, with the drawable origin added at `:670-678`),
relative motion accelerated and unaccelerated (`:647`, `:710`), touch
(`:1476`, `:1519`) and tablet position and tilt (`:2221`, `:2268`).

Note the gate in the fourth mechanism: the window has to *be* the emulated
size. Under EWMH fullscreen the window manager decides that, and KWin sizes it
to the output. **KWin 6.3.6 does not know the property at all** — the atom is
absent from `atoms.h` and the string appears in neither `kwin_wayland` nor
`libkwin.so.6.3.6`. So even a client that does set a mode gets the enlargement
only if something else holds its window at the emulated size, which is what
this effect's X11 resize does and why the earlier prototype saw Xwayland
performing the enlargement itself.

**What KWin would have to do to provide both itself.** The pointer focus path
already carries a matrix: `PointerInputRedirection::focusUpdate` passes
`Window::inputTransformation()` to
`SeatInterface::notifyPointerEnter(surface, position, QMatrix4x4)` — both calls
are in that function in the shipped library, the declarations are
`window.h:829` and `wayland/seat.h:252` — and KWin refreshes it through
`setFocusedPointerSurfaceTransformation` when the focused window's geometry
changes. A scale would fit in that matrix. But `Window::inputTransformation()`
is a translation by minus the window position and nothing else — identity
followed by `QMatrix4x4::translate` in the disassembly — it is neither virtual
nor settable, and no effect API reaches it.

The rest of the input path does not take a matrix at all:

- `Window::mapToLocal` (`window.h:640`) feeds `Window::hitTest`,
  `PointerInputRedirection::applyPointerConfinement` and
  `updatePointerConstraints`, so the confinement and lock regions a game states
  in its own coordinates would land in the wrong place — and a first-person
  shooter locks the pointer.
- `SeatInterface::notifyTouchDown` (`seat.h:531`) takes a surface position and
  no transformation.
- `SeatInterface::relativePointerMotion` (`seat.h:369`) is handed the raw
  delta. Xwayland scales that delta; whether KWin should is a decision, not a
  detail, because it is what the game's mouse look feels like.
- The window's input region would have to cover the presented area rather than
  the X window, or the pointer would reach only the top-left 2560 x 1440 of the
  screen.

Effects have nothing for any of it: `EffectsHandler` offers
`startMouseInterception` with `Effect::windowInputMouseEvent`, which takes all
input away from the window rather than transforming what reaches it, plus
shortcut registration; `EffectWindow` exposes no input surface. The scene holds
the concept the input path lacks — `Item::setTransform`, `Item::mapFromScene`,
`SurfaceItem::setDestinationSize` — but input mapping does not go through the
scene.

**So the fourth link is an upstream KWin change, and it is a legitimate
answer.** Two shapes, which are worth keeping apart:

- Honouring `_XWAYLAND_RANDR_EMU_MONITOR_RECTS` when sizing a fullscreen X11
  window. Small, self-contained, and it makes Xwayland's existing emulation —
  enlargement and input mapping both — work under KWin for clients that do set
  a mode. It does nothing for Source, which never sets one. Worth proposing on
  its own merits.
- A per-window presentation scale that the input path honours: the focus
  matrix, `mapToLocal`/`mapFromLocal`, hit testing and input region, pointer
  confinement and constraints, relative deltas, touch and tablet. The tidy form
  is to map input through the same transform the scene already applies to the
  window item, so one transform serves picture and pointer.

With either in place the X server's own answers stay consistent, because
Xwayland reconstructs root coordinates as the drawable origin plus the
surface-local position it is handed. That is exactly as shallow as Xwayland's
own emulation, which never changes the real root size either.

## Remaining work

### SuperTuxKart in all six presentations, 2026-09-21

This slice owns the handbook's hard requirement
[SuperTuxKart in every presentation it offers](../upscaling.md#supertuxkart-in-every-presentation-it-offers):
native Wayland and Xwayland, each with OpenGL fullscreen, Vulkan borderless
and Vulkan exclusive fullscreen. The game keeps its own settings, the buffer
KWin receives is smaller, the output capture shows it correctly enlarged, and
one automated command runs all six cells.

| Cell | Through the effect today | Evidence |
| --- | --- | --- |
| Wayland, OpenGL, fullscreen | Buffer reduced by `AdvertisedMode` | Runs of 2026-09-18 and 2026-09-19; never with an output-capture check |
| Wayland, Vulkan, borderless | **Fails.** The entry names `AdvertisedMode`, which this cell ignores, and `autoRatio()` sends the fractional scale only for a slot on Auto | The lever works; see below |
| Wayland, Vulkan, exclusive | Not run through the effect | The run of 2026-09-18 changed the game's own resolution, which the requirement excludes |
| Xwayland, all three | Not run | — |

**The earlier reading of SDL 2 was wrong.** This slice, application profiles and
`waylandscale.h` say that SDL 2 never implemented `wp_fractional_scale_v1`. SDL
2.32.4 binds it for every window (`SDL_waylandwindow.c`, `Wayland_CreateWindow`)
and acts on it for a window created with `SDL_WINDOW_ALLOW_HIGHDPI`, which
SuperTuxKart sets for both renderers. For a window that is not in exclusive
fullscreen, `GetBufferSize()` makes the buffer the window size times the
preferred scale, and `ConfigureWindowGeometry()` sets a viewport back to the
window size. A changed scale sends `SDL_WINDOWEVENT_RESIZED`. SuperTuxKart's
`CIrrDeviceSDL::handleNewSize()` then sees a new native scale, and
`GEVulkanDriver::OnResize()` rebuilds the swapchain from
`SDL_Vulkan_GetDrawableSize()`. Exclusive fullscreen takes its buffer from the
selected mode and ignores the scale, so the OpenGL cell still needs the
advertised mode. The earlier scale test, `stk-lever-scale`, ran the OpenGL
renderer only, which is why it saw no effect.

Observed on 2026-09-21 with the game probe, not yet with the effect: a nested
KWin 6.3.6 on its virtual backend at 3840 × 2160 with the real GPU, and
SuperTuxKart 1.4 with `render_driver="vulkan"`, `vulkan_fullscreen_desktop="true"`,
fullscreen at 3840 × 2160 and `SDL_VIDEODRIVER=wayland`. The window committed
3840 × 2160. After `Window::setNextTargetScale(0.5)` the client received
`preferred_scale(60)`, kept its viewport destination at 3840 × 2160 and
committed 1920 × 1080, and the window stayed fullscreen at 0,0 3840 × 2160.
The log is `build/game-probe/stk-vk-scale.log`.

Next, in order:

1. A slot that names an advertisement falls back to the fractional scale for a
   window the advertisement did not reach, one still drawing at full size. A
   window whose buffer the advertisement already made smaller is never asked.
2. A matrix test that runs all six cells with the production plugin in a
   nested KWin and checks both the buffer and an output capture against the
   frame the game drew.
3. Correct the SDL 2 claim in `waylandscale.h`, in the handbook and in the
   catalogue's note for SuperTuxKart.

### Auto, and the lever it would use

The settings model in
[application profiles](slice-application-profiles.md#the-methods-stay-and-auto-is-a-new-one)
gives every presentation slot an `Auto` value, and Auto is a method of its own
rather than a choice among the existing ones. Its X11 half is the resize this
slice already implements, with verification and a revert. Its Wayland half is
the fractional scale described under
[a reversible Wayland lever](#a-reversible-wayland-lever-for-auto-2026-09-20),
and none of it is measured yet.

In order: confirm that `Window::setNextTargetScale()` is reachable from an
effect on v6.3.6 without patching KWin, because the route is closed for the
supported target if it is not; run the seven-item bench; then implement the
ladder if the bench supports it. The incoherent advertisement that review found
— a falsified `wl_output.mode` beside a truthful `xdg_output` — is a defect in
shipped code and is fixed on its own schedule, not as part of Auto.

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

### Presenting a resized X11 window without Xwayland's emulation

Asked for by Jens on 2026-09-19, after the analysis above: force the game to
the wished resolution from the plugin alone, with no change to the game's
configuration and no proxy compositor. The engine already renders at whatever
size its X window has, so the request itself is not the problem; what is
missing is that nothing presents the smaller window across the output and
maps input back to it when the client never asks Xwayland for a mode.

**Start state.** `X11Resize` holds the client's X window at the requested size
and keeps KWin's fullscreen frame at the output. Validation then requires the
emulated mode: without it the surface is smaller than the frame, eligibility
refuses `ResizedSurface`, the request is refused and geometry restored. Left 4
Dead 2 and every other client that takes its fullscreen size from the window
manager therefore ends there.

**End state.** For a window under a live `X11Resize` request whose surface is
smaller than KWin's frame, the effect paints the supplied buffer over the whole
frame and maps pointer input to the buffer's coordinates itself. A client that
does establish the emulated mode keeps being presented by Xwayland, with no
change in behaviour. Status names which of the two is presenting.

**Approach.**

1. Validation accepts a second success shape: the supplied buffer is the
   requested size, KWin's frame still covers the output, and the surface is
   presented at the buffer's own size. The refusal for a missing emulated mode
   goes; the retry and refusal for a buffer that is not the requested size stay.
2. A window under a live request is marked through `EffectWindow::setData`,
   and the surface check waives `ResizedSurface` for a marked window whose
   surface is smaller than its frame. The scaler already reads the buffer at
   its own size and paints the frame, so the paint path needs no change.
3. Input: an `InputEventFilter` installed ahead of KWin's forwarding filter.
   Its only work, per pointer event whose focused surface belongs to a marked
   window, is to replace the seat's focused-surface transformation with a
   translation to the buffer origin followed by a scale of surface size over
   frame size, and to scale relative deltas by the same factor, which is what
   Xwayland's own emulation does. KWin's focus logic is untouched: it already
   believes the window covers the output, so hit testing and enter/leave are
   right and no event has to be delivered by the effect itself. The factor is
   derived from the same geometry the scaler uses, so it is one while Xwayland
   presents the window and the two paths cannot double-scale.
4. Touch, tablet, pointer confinement regions and the locked-pointer position
   hint are not mapped in this step; they are recorded as limitations.

**Planned, not yet observed.** On wzpc, one test at a time: Left 4 Dead 2
menu navigation first, mouse look in play second. Both containers, both
compilers, warnings as errors, on the rebuilt Trixie image.

**Implemented on 2026-09-19.** `x11input.cpp` holds the filter; the
controller marks a window under a live request with `upscaleResizedRole`,
answers `presentedUnder()` for the filter and `presentation()` for status;
`x11resolution_validate.cpp` accepts the second success shape; the surface
check in `eligibility.cpp` waives `ResizedSurface` for a marked window whose
surface is smaller than its frame. The handbook's X11 resize section describes
both presentations and the input mapping.

Two things the virtual backend taught before the test passed, both now in
source comments. KWin's hit test goes through the surface's own input region,
not the frame, so over the part of the output the small surface does not
occupy KWin focuses nothing at all; the filter therefore establishes the
seat's focus itself there, and withdraws it when the pointer leaves the frame
or KWin finds a window of its own on top. And at the moment a request begins
the surface can still carry a 1 x 1 placeholder, so the scale is only taken
from a surface that is the requested size, which is also the only surface the
scaler paints.

**Observed, virtual backend on this machine (KWin 6.3.6, `build/native`,
2026-09-19).** `upscale-x11-integration`: 11 passed, 0 failed. The new
`presentsWithoutEmulation` run, with a client that never sets a mode: the
window held 1920 x 1080 for 3.5 seconds past negotiation, status reported
`presented by this effect, pointer input mapped`, no request failure, the
buffer was captured, a pointer moved to 1920, 1080 arrived at the X client as
960, 540, and after the request was released a pointer at 1930, 1090 arrived
unscaled. `upscale-snapshot`: 10 passed. Getting that far needed two test
fixtures: a pointer device in the test driver, because a seat without one
offers clients no pointer at all, and a `wl_pointer.frame` after each injected
motion, because Xwayland acts on a motion only when its frame arrives.

**Observed, containers.** KDE neon unstable (KWin master): GCC and Clang
builds with warnings as errors passed, render tests passed; the test driver
and the X11 integration test do not build there by design. Debian Trixie: the
first run failed in configure because the local check image predated the
`libxcb-randr0-dev` build dependency; results of the rebuilt image are
recorded below when observed. The lint stage's commit-level hooks reported
findings only in `tools/measure-frame-times.py` and its test, which this
slice did not touch: a shebang without the executable bit, a spelling
codespell rejects, and two files ruff-format would rewrite. That stopped the
runner before the pre-push hooks, which are run separately below. They are
fixed with this work, because the branch cannot go up for review red.

**Observed on pcjensd, first real run, 2026-09-19 22:43.** Left 4 Dead 2
with the profile enabled, build `0.1.0+git20260919.5a082be4b2-dirty`
installed by Jens. Status while the game was active: `2560 × 1440 requested
… presented by this effect, pointer input mapped`, `Supplied input: 2560 ×
1440`, `FSR 1`. The X window was 2560 x 1440 at the origin with
`_NET_WM_STATE_FULLSCREEN` and no emulation property, and `xwd` of that
window (a query to the X server, nothing attached to the game) gave a
complete 2560 x 1440 frame of the game's video menu. `video.txt` still said 2560 x
1440 fullscreen. What Jens saw on the screen, though, was the enlarged
picture cut off: only its top-left 2560 x 2160 pixels. KWin's
`WorkspaceScene::paintSimpleScreen` intersects each window's paint region
with its item's bounding rectangle (read in the 6.3.6 binary: `QRegion
&= Item::mapToScene(Item::boundingRect()).toAlignedRect()`), and the item was
the surface's own size. The effect's paint was therefore clipped, whatever it
drew.

The fix, implemented the same evening: once the requested buffer has arrived
and the client established no emulated mode, the controller sizes the surface
item to the frame (`SurfaceItem::setDestinationSize`), which is exactly what
Xwayland's viewport does for an emulated mode; KWin then treats the window as
covering the output for painting, damage and opacity. The pointer scale is
taken from the requested size against the frame in X pixels, and who presents
is decided once, from the emulation property, when that buffer first arrives.
The `upscaleResizedRole` mark and the `ResizedSurface` waiver went again.
Virtual backend after the fix: 11 passed, 0 failed, with the cooperative
clients still presented by Xwayland and the non-cooperative one by the effect
at the scaled pointer position. Not yet observed on the real machine.

**Limitations, by design of this step.** Where KWin's own hit test does not
find the window - the part of the output outside the small surface - KWin
shows its fallback cursor rather than the client's, and grants no pointer
lock or confinement, because both follow KWin's own focus. Relative motion
reaches the client regardless, scaled like Xwayland scales it. Touch, tablet,
confinement regions and the locked-pointer position hint are not mapped.
Crossing the edge of the small surface produces a leave and enter pair for
the client. Whether Source's mouse look is content with that is the second
real-device test.

## Source-led compatibility investigations

Requested by Jens on 2026-09-19: turn the known gaps into concrete investigations
using open-source applications and their source code. This is follow-up work
within resolution control, not a claim of universal support or permission to
patch another component. The durable compatibility boundary is in the
[handbook](../upscaling.md#known-compatibility-limits).

Starting evidence includes the measured negative glmark2 and SuperTuxKart
cases, Tux Racer's primary-output migration and SuperTux's fixed intermediate
target above. Source inspection identifies why these are different problems.
The supported-scope end state for each investigation is a reproducible result
and either a tested compliant implementation or a precise source-backed limit
with safe refusal and truthful diagnostics. A limit does not close the broader
application-compatibility requirement. Full acceptance additionally needs the
production plugin and package on real outputs, with rendering and input verified.

For every task, pin application/toolkit/driver versions and backend, begin with
the unmodified application, and change only plugin settings for the requested
resolution. Test-only API tracing and backend selectors establish evidence;
they must not become required user launch instructions. Record the calculated
target, protocol/mode state, native geometry, submitted buffer, full-output
destination and actual render targets separately. Repeat 1080p and 1440p on 4K,
then disable and unload. Keep an unrelated fullscreen application running on
the other output with a Native rule; verify its normal dimensions throughout.
Also exercise threshold equality, an ultrawide output above the pixel limit,
threshold disabled and independent rules. Use private test configuration and
preserve normal desktop modes and scales.

Investigate in the following order, first reproducing each gap with the
production controller. A source hypothesis is not an observed compatibility
result. Every proposed fix must satisfy the plugin-only, per-application,
no-external-patches, no-game-reconfiguration and Debian-delivery requirements.

- [ ] **X11 non-cooperation — glmark2 2023.01, X11 backend.** The research
  negative test already observed a smaller drawable with a stale 4K viewport
  and no full-output emulation. Read `NativeStateX11::create_window()` and
  `should_quit()` in
  [native-state-x11.cpp](https://github.com/glmark2/glmark2/blob/2023.01/src/native-state-x11.cpp):
  fullscreen dimensions come from the root window, and the event loop handles
  keys/close messages rather than resize notifications. Reproduce bounded
  refusal/restoration in the production plugin, trace its GL viewport and
  buffers, and inspect whether any existing client-consumed window protocol
  can trigger a renderer rebuild. Changing the shared root dimensions,
  injecting calls or requiring a wrapper is not a compliant solution. If no
  such protocol exists, preserve this as the explicit non-cooperating example.

- [ ] **Wayland fullscreen-desktop — SuperTuxKart 1.4, Vulkan versus OpenGL.**
  The Vulkan run retained a 4K buffer after a 1080p mode advertisement; OpenGL
  responded. Read the game's `vulkan_fullscreen_desktop` selection, SDL's
  Wayland configure/scale handlers and
  [GEVulkanDriver's swapchain creation](https://github.com/supertuxkart/stk-code/blob/1.4/lib/graphics_engine/src/ge_vulkan_driver.cpp).
  Inspection confirms both initialization and swapchain recreation use
  `SDL_Vulkan_GetDrawableSize`. Trace fullscreen configure dimensions,
  fractional/preferred scale, viewport and swapchain extent together. Determine
  whether KWin's exported target-scale/geometry APIs can coordinate a smaller
  buffer with unchanged logical coverage and correct input. Test both KWin
  targets; toggling the game's fullscreen-desktop setting is a comparison,
  not a delivered fix. Do not generalize this failure to all Vulkan clients.
  Answered for Wayland on 2026-09-21: the fractional scale reaches this cell;
  the work continues under
  [SuperTuxKart in all six presentations](#supertuxkart-in-all-six-presentations-2026-09-21).

- [ ] **Integer-scale reachability — glmark2 2023.01 Wayland and vkmark 2025.01.**
  Read their Wayland output/configure handlers and the plugin's
  `reachableScale()`/`advertisementFor()` calculation. Reproduce scale-1 refusal
  and discrete reachable sizes at desktop scales 1.5, 2 and 3. Inspect use of
  `wp_fractional_scale_v1` and `wp_viewporter` before trying preferred scale or
  coordinated target-scale changes. A solution must keep desktop scale and
  unrelated clients unchanged and report exact versus adjusted targets. A
  client lacking the needed protocol support remains unsupported by that route.

- [ ] **Secondary output and missing modes — Extreme Tux Racer 0.8.4/SFML 2.6.2.**
  Read `states.cpp`, `CWinsys::SetupVideoMode()`,
  [Window::create()](https://github.com/SFML/SFML/blob/2.6.2/src/SFML/Window/Window.cpp)
  and [WindowImplX11::setVideoMode()](https://github.com/SFML/SFML/blob/2.6.2/src/SFML/Window/Unix/WindowImplX11.cpp).
  Inspection confirms mode validation and explicit primary-output selection;
  the measured secondary-output migration is why the shipped guard exists.
  Establish whether any existing per-client mechanism lets this SFML version
  select the intended output without changing the session's primary output.
  Separately test absent odd-sized modes and inspect mode-list ownership before
  considering a clearly reported nearest-mode policy. Do not silently substitute
  a mode or remove the primary guard without a two-output regression and a real
  game trace. If a framework patch is the only solution, record that boundary.

- [ ] **Borderless presentation — SuperTux 0.6.3/SDL2 on X11 and native Wayland.**
  X11 research demonstrated reduced outer buffers in a borderless window;
  native Wayland is a candidate still requiring reproduction. Follow
  [SDLBaseVideoSystem::create_sdl_window()/on_resize()](https://github.com/SuperTux/supertux/blob/v0.6.3/src/video/sdlbase_video_system.cpp),
  its fullscreen-desktop branch, SDL backend resize handling and KWin's
  destination/input mapping. Test a normal undecorated full-output window and
  fullscreen-desktop separately; record actual KWin fullscreen state rather
  than treating the game's label as that state. Determine which route retains
  output coverage after resizing. A normal smaller window, a multi-output
  spanning window or a resize that loses coverage must remain ineligible;
  menu changes are not an automatic production solution.

- [ ] **Internal rendering cost — SuperTux 0.6.3 OpenGL.** The observed
  1368 × 769 intermediate framebuffer stayed fixed while the outer buffer
  changed. Read `ScreenManager::process_events()`,
  [GLVideoSystem::apply_config()](https://github.com/SuperTux/supertux/blob/v0.6.3/src/video/gl/gl_video_system.cpp),
  `Viewport::from_size()` and `GLTextureRenderer`. The first two sources show
  resize/configuration processing and intermediate allocation from the logical
  viewport. Trace each pass's target and viewport, then measure GPU frame time
  with the same scene. Establish which work actually falls with output size;
  do not infer proportional savings or internal-resolution control from swaps.
  If those targets are application-owned policy, document that no compositor
  request can promise to resize them under the current constraints.

- [ ] **Wine/Proton presentation — OSS D3D probes and a real game.** These are
  investigation candidates, not demonstrated failures of the current plugin.
  Start with Windows SuperTuxKart for OpenGL/Vulkan and
  [dxvk-tests' D3D11 triangle](https://github.com/doitsujin/dxvk-tests/blob/257d9e47c1e6092df1f5da4ab36367769cb13f35/d3d11/d3d11_triangle.cpp).
  The latter reads `GetClientRect` and calls `ResizeBuffers`, making it a useful
  cooperating comparison. For D3D12 inspect
  [vkd3d-proton's triangle demo](https://github.com/HansKristian-Work/vkd3d-proton/blob/5d0db7414b0b3f1afa7c9a84acf9ff483cb805d1/demos/triangle.c)
  and its Win32 swapchain helper: the demo initializes a fixed 640 × 480
  viewport, so first establish which resize/fullscreen variants it can actually
  exercise. Do not count that fixed-size demo as a successful fullscreen game.
  Trace the game's target, DXGI/Vulkan swapchain and Wine's host presentation
  buffer separately; read Wine's X11/Wayland display paths and fullscreen blit
  code. Run Wine and official Proton separately, with backend identity proved.
  Existing nested-compositor experiments do not establish plugin-only support.

Physical mixed-scale outputs, movement/hotplug and pointer confinement remain
acceptance tasks below. HDR/VRR and image/performance acceptance retain their
owner in the [rendering slice](slice-fsr1-hdr-vrr.md); source review cannot replace
those checks. No new live compatibility result is claimed by this task list.

### Extreme Tux Racer's own fullscreen is the wrong shape, 2026-09-20

Observed on pcjensd, one 3840 x 2160 output, effect build
`0.1.0+git20260920.e652374`. With `~/.config/etr/options` carrying
`[fullscreen] 1` and `[res_type] 0`, the game comes up at 1024 x 768. That is
4:3 against a 16:9 output, and the effect refuses a buffer whose aspect ratio
does not match the destination, so no run of it measures the game: the readings
describe whatever else the effect was following, at the compositor's idle rate.

Without that options file the effect's own X11 resize decides the size instead,
and the same game was observed at 1920 x 1080 into 3840 x 2160 with FSR 1 active
while a course was running. So the path works; it is the game's stored
resolution that does not, and `res_type` selects from a list this project has
not read. Setting it by guess is not worth the risk of a run that looks
configured and is not.

This does not change what the game can show. It commits 59.8 buffers a second
in its menu and on its course alike, at every preset, so it is frame-limited
and reports headroom rather than cost whatever resolution it renders at.

### What a reload with control off looks like, 2026-09-20

The nightly's resolute job failed the lifecycle case a second after the effect
was reloaded with `ResolutionControl` false, and the assertion now carries the
effect's own status, which named the state rather than a rectangle:

    status: Desired: Select 1920 x 1080 in the game
    Supplied input: 1920 x 1080   Destination: 3840 x 2160   FSR 1
    metrics: scaling=1 selected=1 supplied=1920x1080 windowsystem=x11

So at that moment the window is still the size the previous request left it,
and the loaded effect is upscaling that buffer. That is not control acting
while disabled: upscaling a small buffer is the effect's other job, and
resolution control governs only whether it asks an application for one. The
window returns to its native size shortly afterwards, on this machine within a
second and on the slower one within the test's bound.

Worth knowing for the product, not only the test: between an effect being
loaded and a client returning to its own size, a user sees an upscaled image
they did not ask for. It is brief, and nothing here establishes how brief on a
machine under load.

### The negotiation does not complete on a machine drawing 3 frames a second

Blocker for the nightly's `resolute` package jobs, 2026-09-20, and not a test
defect. Once the session was allowed to run to completion - the harness had
been killing it at 90 s - the run finished in 146 s with two cases failing for
the same reason:

    lifecycle(primary-fullscreen)  x11_integration_test.cpp:135
      expected 2560 x 1440, actual 3840 x 2160, after waiting 30 s
    repeatedFullscreenTransitions  x11_integration_test.cpp:297
      metrics: presented=3.14  worst=3166.667  supplied=3840x2160
               scaling=0  selected=0

The runner presents 3.14 frames a second, one frame every 3.17 seconds. This
control validates a request 3 seconds after issuing it. A client that draws
once every 3.2 seconds cannot answer inside that window, so validation fails,
the retry re-issues, that fails too, and the request is refused. The plugin is
behaving as designed; the design assumes a client that draws faster than the
window it is given.

No bound inside a test can change that, and the three ways out are decisions
rather than fixes:

- **Make the validation window follow the client.** Time it in the client's
  own frames rather than in seconds - a request judged after, say, two commits
  or three seconds, whichever is longer. This is the only option that makes the
  feature work on a slow machine rather than merely stop testing it there.
- **Skip these cases where the machine cannot sustain the negotiation**, with
  the measured rate in the skip message. Honest, and it keeps the quality jobs
  covering the behaviour, but a regression could hide behind the skip.
- **Do not run these tests in package builds at all**, leaving them to the
  quality jobs. Cheapest, and it weakens what a package build verifies.

Jens's call. Nothing here should be widened further in the meantime: four
rounds of larger numbers each moved the failure to a different line, and the
timeout that was actually ending the runs was the harness's own.

### A reversible Wayland lever for Auto, 2026-09-20

Jens asked whether the effect can work out by itself which request a native
Wayland game needs. The answer that came back is that it cannot do so with the
advertisements - a falsified `wl_output.mode` is sent before the client has a
window and cannot be taken back - but that a different lever exists which is
sent *after* the window, aimed at one surface, and reversible.

Everything in this section was **read, not run**: the upstream sources of SDL 2,
SDL 3, GLFW, QtWayland, Godot, `winewayland.drv` and SuperTuxKart 1.4, and KWin
6.3.6 and master under `build/upstream/`, on 2026-09-20. The bench at the end is
what turns it into evidence. The client classification it rests on is recorded
with the settings model in
[application profiles](slice-application-profiles.md#what-a-wayland-client-actually-reads-source-review-2026-09-20).

#### Waiting and then advertising does not work

The first idea was to say nothing at bind, wait for the window, and only then
send the mode for the presentation we can now see. The sources say a running
client does not act on it:

| Toolkit | A `wl_output.mode` after the window exists |
| --- | --- |
| SDL 2 | Ignored. It processes only the `done` it was waiting for during initialisation and the counter saturates; even reprocessed, setting the desktop mode is a copy and no window geometry is recomputed |
| SDL 3 | The display is rebuilt and the application receives `SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED`, but nothing re-runs the fullscreen update for windows already on that display. A game that does not handle the event itself does not change, and games rarely do |
| GLFW | `glfwGetVideoMode()` returns the new value; no callback and no window change |
| Qt | Irrelevant while `xdg_output` is present, which is how it takes screen geometry |
| Godot | Screen data updated, windows untouched |
| Wine | Every `done` re-registers the Windows display devices, so the game sees `WM_DISPLAYCHANGE`. Whether it resizes is the game's business, and if it does the result is class D's undersized surface unless the scale half is sent too |

#### The lever: a fractional scale below one

`wp_fractional_scale_v1.preferred_scale` is "the numerator of a fraction with a
denominator of 120" and the protocol states no lower bound, unlike
`wl_surface.preferred_buffer_scale`, which must be greater than zero and which
KWin sends as `ceil(scale)`. So a fraction below one is expressible where an
integer scale is not, which is exactly the gap on a 4K television at scale 1.

KWin already has the whole path. `SurfaceInterface::setPreferredBufferScale`
sends `round(scale * 120)`, driven by `Window::setNextTargetScale()`, and a
change schedules a new configure that carries the scale. An effect can reach the
window through `EffectWindow::window()`, which `eligibility.cpp` already does.

Three things to settle before building on it, in this order:

1. **Whether `setNextTargetScale()` is reachable from an effect in v6.3.6**, the
   minimum supported target, without patching KWin. If it is not public there,
   this route is closed for the supported target whatever master does, and
   `compatibility.h` is where any difference between the two would be handled.
2. **Re-assertion.** KWin re-applies the output's own scale in
   `updateNextTargetScale()` whenever the window changes output or the output's
   scale changes, so an override has to be re-asserted on
   `nextTargetScaleChanged` or it is silently undone.
3. **Rounding.** KWin uses the same value in `snapToPixels()` for configure
   sizes. It is exact for 1/2 and 2/3, so Performance and Quality are clean;
   1/1.3 and 1/1.7 introduce sub-pixel rounding that has to be measured against
   the aspect-ratio tolerance the scaler already applies.

Which clients act on it, from the same source reading:

| Acts on a preferred fractional scale | Does not |
| --- | --- |
| GLFW, by resizing the framebuffer; on by default in 3.4 | Qt, which clamps the value to 1.0 |
| SDL 3, but only for a window with high pixel density or scale-to-display | SDL 2, which never implemented the protocol |
| Godot, which updates the window state | |
| Wine, which remaps the window - but only coherently if the mode half was falsified as well | |

The handbook's earlier note that a fractional scale hint failed was measured on
SDL 2, which cannot honour it, and the Qt result is explained by the clamp.
Neither says anything about the lever itself.

#### The ladder Auto would follow

For a profiled program on an unscaled output, with the slot on Auto:

1. **At bind, say nothing.** Auto never borrows a measurement from the other
   Wayland slot: review on 2026-09-20 rejected an earlier draft that did,
   because one program can be a mode-list client in one presentation and a
   configure-sized one in the next - SuperTuxKart is exactly that. Wine never
   gets the mode from Auto in any case, because it is class D and because it
   cannot be identified at bind at all: the connection belongs to the Wine
   loader and the game's name arrives later as the window's `app_id`.
2. **At the first commit**, with window, presentation and identity known and
   `upscalePresentation()` true, ask for the fractional scale equal to the wish
   and re-assert it on `nextTargetScaleChanged`. Then watch the next commits:
   - the buffer shrank and the surface still covers its output: success, and
     remember it for this session only;
   - the buffer is unchanged after a couple of configures: the client ignores
     the hint, so set 1.0 back and report that no method reached it;
   - the surface stopped covering: revert at once and report.
3. **Windowed presentations: never.**

**Why coverage is the input check as well, on this lever.** The handbook counts
three things as success: the buffer got smaller, the image still covers the
screen, and the pointer still lands where it looks. The third is checked
separately for the X11 resize, because there the effect changes the window's
size and has to map pointer input itself. The fractional scale changes nothing
input is measured in. Pointer events reach a Wayland surface in its own
logical coordinates, and the lever moves only the buffer behind that surface:
a client that honours it renders `logical × scale` pixels and declares the
viewport back to the logical size, which is what `wp_fractional_scale_v1`
requires it to do. So while the logical size stays the output's, input lands
where it looks by construction, and a client that changed its logical size
instead is exactly the one the coverage check catches. That argument is the
protocol's, not a measurement, and the bench below checks it with a pointer hit
test rather than taking it on trust.

#### Two other levers, and why they are not it

`ClientConnection::setScaleOverride()` already scales `xdg_output` size and
position, surface sizes, input and opaque regions, pointer and constraint
coordinates per client - but not `xdg_toplevel` configure sizes and not
`wl_output.mode`, because Xwayland needs neither. For an xdg-shell client the
result is therefore incoherent, and making it coherent is a change to KWin
rather than something an effect can do.

`xdg_toplevel.configure_bounds` steers only the initial floating size, which is
irrelevant to fullscreen and borderless and marginal for a windowed slot.
Viewporter is client-side; fifo, commit-timing and presentation-time carry no
size at all.

#### A defect this review found in shipped code

The advertisement is incoherent: `wl_output.mode` is falsified while
`xdg_output` continues to report the output's true size. SDL 2 believes the
falsified value only because `announce()` runs synchronously inside
`OutputInterface::bound`, so ours is the `done` it happens to process; had the
`xdg_output` one arrived first it would have derived a scale factor from the
disagreement instead. SDL 3 processes both and ends up with a mode list holding
the true and the falsified size together, which makes what its exclusive-mode
matcher picks unpredictable without running it. This is independent of Auto and
wants fixing on its own.

#### The bench that settles all of it

Each application is run three ways - mode at bind, fractional scale after the
window, and both - with a late-mode negative control, recording the advertised
size, the committed buffer, whether the surface still covers its output, and
whether a pointer hit test still lands where it looks:

1. A GLFW 3.4 program fullscreen with a monitor, and undecorated at video-mode
   size: class A against class C, in one toolkit.
2. SDL 3 `testsprite --fullscreen`, with and without high pixel density, and
   with an exclusive mode, which also resolves the ambiguous mode list above.
3. SuperTuxKart 1.4 on OpenGL and on Vulkan, which is class B against class A in
   one program and tests the window-flag explanation directly.
4. A Godot 4.3 or later export with `--display-driver wayland`, fullscreen.
5. Wine 10 with `winewayland` and a DXVK sample, borderless and exclusive. Note
   that `ChangeDisplaySettings` fails there unless `EmulateModeset` is set.
6. vkmark with the mode plus a *fractional* scale in place of the integer one,
   to confirm the scale-1 gap actually closes for class D.
7. A Qt Quick fullscreen sample as the negative control for the clamp.

Beyond launch configurations, each of Auto's own transitions has an expected
result, so that a run can fail rather than only record:

| Transition | Passes when |
| --- | --- |
| The client honours the hint | the committed buffer shrinks within a few frames, the surface still covers its output, a pointer hit test at the centre and at one corner lands on the pixels drawn there, and the status reports the fractional scale as the method that reached it |
| The client ignores the hint | within `patienceInFrames` the preferred scale is back at the value the window had before, and the status says that no method reached the client rather than reporting a pending request |
| The surface stops covering its output | the preferred scale is restored on the next commit, not after the patience runs out, and the status names the lost coverage |
| KWin reapplies the output's scale | after moving the window to another output, or changing that output's scale, `nextTargetScale()` returns to the requested value within one configure |
| Configuration moves | after `reconfigure()` every window's preferred scale equals the value recorded before the request, with no request left standing |

The Qt sample is the natural source of the second row, since Qt clamps the hint
to one, and a GLFW program honours it, so it serves for the first, third and
fourth.

### Auto under test, 2026-09-21

Until this date no automated test drove Auto at runtime: every integration case
named its method. Two cases were added - `autoAsksTheWindowForAFractionalScale`
in the Wayland integration test, whose client now binds
`wp_fractional_scale_v1`, and `autoResizesAnUnmeasuredX11Window` in the X11
one - and the Wayland one found four defects, all in the Wayland half:

1. **A window drawing at full size was never asked.** Auto asked the candidate,
   and a full-size buffer is exactly what keeps a window from being the
   candidate. It now asks the window that qualifies in every respect but its
   buffer, `upscaleWindowAwaitingBuffer()`.
2. **Every release was undone at once.** `release()` gave the scale back
   before forgetting the request, and the `nextTargetScaleChanged` handler that
   re-asserts a standing request put it straight back - after the patience ran
   out and on lost coverage alike. The request is now forgotten first.
3. **An ignored request was asked again every thirty frames.** Released and
   forgotten, the next frame asked afresh. The request now stays, marked as
   ignored, until the window closes, the ratio changes or the settings do.
4. **Auto depended on something else keeping the effect active.** KWin calls
   only an active effect's paint hooks, which is where Auto asks and counts;
   with no candidate and no display showing, it never ran - which a Debug
   build's default frame rate display hid. `isActive()` now also holds while
   Auto has a window to ask or is waiting for an answer, and a window that
   stops qualifying - out of fullscreen, off its output, minimized, resized -
   gets its scale back from its own signals rather than from a paint that may
   not come.

The status also reports what Auto asked for: the surface scale on Wayland, and
the window size on X11, which it previously reported only for a method named
outright. Observed: both cases pass on Trixie. Jens's report of Auto working on
2026-09-21 is consistent with this: on X11 it was unaffected, and a Debug build
keeps the effect active. Which session it was is still to be recorded.

### The X11 request path on KWin 6.6, made deterministic, 2026-09-21

The X11 test on Ubuntu 26.04 failed intermittently: interleaved against
7f55154 on one machine, the tree then current passed 3 of 5 and 7f55154 5 of 5,
though 7f55154 also once took 144 s instead of 62. Traced by Fable on KWin
6.6.6, three defects in the X11 control, none reachable on 6.3.6, which never
reads the emulation property:

1. A client's ConfigureRequest that arrived during the withdrawal wait went to
   KWin, which answered with its stale cached geometry. The mirroring client
   set that as its mode, KWin enforced it, and the effect re-armed its
   withdrawal wait every 3 s for good. The filter now answers such requests
   with the true geometry.
2. A reconfiguration released a request the client had not answered yet, and
   the client's late answers then raced the new request. A release now waits
   for the client's answer or the end of the validation window, and
   `settled()` includes pending releases.
3. The withdrawal wait's fallback never made the request it promised. It now
   does, once, and validation judges the result.

The test's `lifecycle` also waited 100 ms after a client resize and asserted;
it now waits for the ConfigureNotify that answers it.

Observed by Fable on the fixed sources: 70 X11 iterations on 6.6.6 without a
failure (30 on the exact final sources), the full 26.04 suite 19 of 19 twice,
Trixie with GCC and with Clang 18 of 18 each, clang-tidy clean. Three
iterations in one pair of concurrent runs took 110-190 s and passed; their logs
were not kept, and the same wall-clock times in both runs suggest an outside
stall. Not established: the cause of the ~1 s stalls in the failing runs, and
why the Auto rework lost the race more often. Not exercised: the hosted runner
and a real session on 6.6.

## Remaining work on the X11 production integration

- [ ] Watch how long the X11 test takes after the KWin 6.6 fix of 2026-09-21,
      agreed with Jens the same day. The pull request's CI runs it only on
      Trixie (KWin 6.3.6); Ubuntu 26.04 (KWin 6.6.6) runs it only in the
      nightly's resolute package jobs, amd64 and arm64. A normal run takes
      about 60 s. If runs there take markedly longer - Fable saw three passing
      iterations at 110-190 s, logs not kept - investigate the slow path:
      keep the full test output of such a run (`ctest --output-on-failure`
      drops it for a pass) and find what the X11 control or the session is
      waiting on. Jens's hypothesis, 2026-09-21: the slow local runs coincided
      with a language model running on this machine's GPU and CPU, so a slow
      run on CI's otherwise idle runners would be the telling one.

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

PR #14 review follow-up: Wayland advertisement history is now keyed by the
actual client connection and output. Another instance of the same executable
cannot replace the selected window’s reported request; client/output destruction
clears its records. Disabling control clears retained history even after a
previous reconfiguration restored the protocol resources. The integration test
starts another connection with a different target while keeping the first
window selected. The combined candidate passed both Trixie compiler suites and the new client-identity regression.

Combined PR validation found that sanitizer LD_PRELOAD propagated from KWin to
system Xwayland/xkbcomp. LeakSanitizer then failed xkbcomp before the X11 client
could run. The private test session now launches Xwayland through a temporary
PATH wrapper which drops only that preload for the distribution helper tree;
KWin, the plugin and the instrumented test client retain sanitizer coverage.
The initial sanitizer run failed; after this harness fix and fixture synchronization, all 15 instrumented runtime tests passed.
FreeBSD package mapping now includes the new XCB dependencies and Xwayland,
checked against the official ports inventory; no new BSD runtime result is claimed.

### Review and validation follow-up, 2026-09-19

The owner-supplied review identified four valid issues in the X11/output state
handling: ConfigureNotify's typed structure omits the final four wire bytes;
colour refusal was not invalidated by output changes; released requests could
remain in status; and repeated fullscreen entry on one window consumed the
replacement budget. These are corrected with a padded 32-byte event, output
change invalidation, request cleanup and distinct-window counting. Selection
and its signal watchers now live in their own source file to retain the file
size limit. Regressions exercise colour recovery without effect reconfiguration,
refused-request status and eight fullscreen transitions on the same window.

The separate claim that blocked geometry updates leave KWin's logical frame
unchanged does not match the inspected 6.3.6 and master implementation:
`moveResizeInternal()` assigns the frame/client/buffer rectangles before the
native configure guard. Retain the non-flushing pair and test the intercepted
fullscreen request from an initially smaller, already-emulated window. Its
logical full-output coverage and actual capture must succeed after settling.
The focused regression passed: eight fullscreen transitions completed in
about 4.6 seconds, with a 1920 × 1080 buffer, 3840 × 2160 logical frame and
viewport destination, a captured frame and no replacement-cap refusal.

Sanitizer testing also exposed a fixture timing issue: initial X geometry could
match before KWin had managed the window and acknowledged fullscreen. The
fixture now waits for management and the test waits for fullscreen state before
checking final monitor placement. Its corrected sanitizer runtime suite passed
before the review fixes, taking about 128 seconds, so instrumented integration
sessions have a bounded 300-second CTest timeout (ordinary runs remain at 100).
A fresh container also exposed missing `/tmp/.X11-unix` initialization: maintained
images now create the standard sticky socket directory because no tmpfiles
service runs there. Test-only Xwayland preload isolation does not change the
installed plugin's launch requirements.

The documentation audit covered all human Markdown documents, active slice
records, configuration, workflows and source claims. Corrected implementation
status, settings semantics, renderer formats, obsolete launch proposals and
acceptance claims; checked local links and anchors. The completed temporary
audit document was removed after both hook stages passed. The source-led tasks
above preserve the owner's requested compatibility investigations. Real-device
acceptance remains in its owning slices. The PR must still pass its hosted checks and exact-revision review after publication;
local results are recorded below.

Final local combined-candidate validation:

- Trixie GCC and Clang: all 16 CTest entries passed; the X11 suite includes the
  timing fix, both-output policy, bounded refusal, restoration and repeated
  fullscreen transitions. Neon GCC and Clang: all 11 available entries passed.
  Every build used warnings as errors and its own environment/compiler directory.
- clang-tidy and plugin metadata schema validation passed, including the new
  selection translation unit. Both pre-commit stages passed, and all 18 remaining
  Markdown documents passed the local file/anchor check.
- Address/undefined sanitizers: all 15 runtime entries passed, including all 12
  X11 test cases. One pre-commit wrapper invocation also noticed a concurrent
  documentation-only edit; that was not a runtime-test failure. The separate
  configured fuzzing hook passed its 60-second run after the tree settled.
- ThreadSanitizer passed all 15 runtime entries in the maintained container with
  CI's seccomp setting. Coverage passed at 93.8% of plugin C++ lines (2296/2449).
- FreeBSD dependency-name translation succeeded with the added XCB/Xwayland
  requirements. No new FreeBSD build or physical-output acceptance is claimed.

The earlier real-game traces and package checks remain the observed evidence for
negotiation and installation; they predate these review corrections. Hosted
package checks validate the submitted source revision separately. Do not treat
this local validation as a new physical Tux Racer or television run.

The follow-up handbook update makes the owner's isolation and broad game/Wine
compatibility goals explicit acceptance gates and corrects a stale single-output
rendering description. Its matrix is planned acceptance, not additional passing
game results. The source-led compatibility tasks above own the known reproducers;
new failures extend those investigations without claiming universal coverage.

The latest review identified stale refusal state when an operating system reuses
a departed client's PID. The fix retains state across prompt XID replacement,
then expires orphaned request/refusal/retry state after three seconds without
another managed window closing. A still-present matching window retains its
state. Validation tokens remain unique across expiry. The added regression
checks that immediate replacement retains refusal and a later same-key launch
can negotiate without reconfiguration. The follow-up passed all 16 Trixie tests
with GCC and Clang, all 11 neon tests with each compiler, focused clang-tidy,
and all 15 runtime tests under address/undefined sanitizers and ThreadSanitizer.
The X11 suite now passes 13 cases. The configured fuzzing hook passed; refreshed
coverage is 93.7% (2317/2473 plugin C++ lines). Hosted checks and review must
still validate the published revision; physical acceptance remains open.

Hosted run `35442425006` passed compilers, instrumentation, static analysis and
the binary package checks, but the source-archive test failed during initial
secondary-output placement. Initial geometry settled just beyond the
assertion's five-second timeout. Synchronize fixture setup with the
fullscreen acknowledgement before inspecting placement, with an explicit bounded
startup timeout. Repeat the virtual integration check and run the required
container/compiler and hook checks before publishing the correction.

The correction passed all 16 Trixie tests with GCC and Clang, warnings as errors,
and three additional consecutive X11 integration runs (68.87, 69.08 and 63.97
seconds). Both neon compilers built and passed all 11 available tests. These
checks exercise virtual sessions only; physical acceptance remains open.
Both full hook stages, clang-tidy and plugin metadata validation passed as well.
The source-archive validation and its environment limitations are recorded in the
[release pipeline](slice-build-release-pipeline.md#hosted-publication-filename-correction-2026-09-19).

The seven open review threads were checked against the current implementation
and resolved individually: non-null catalogue assertions, documented catalogue
count, asynchronous window picking, cleared output measurements, per-client/output
advertisements, plural-aware frame counts and departed-client state expiry are
present with regression coverage. The two earlier findings outside the diff
(historical rendering status and scoped GL allocation errors) are fixed too.
No review was dismissed and no approval override was requested. Every push still
requires a fresh approval for its exact revision.

- [ ] Implement the `UserConfigured` method specified in the
      [handbook](../upscaling.md#letting-the-user-choose-the-resolution-in-the-game):
      the method value and its editor meaning, a preset that reads as a
      recommendation rather than a request, and status that reports the size
      actually supplied beside the one recommended. Asked for by Jens on
      2026-09-19, for games no per-client request can reach. The scaling half
      needs nothing: a smaller buffer is already scaled whoever chose its size.

### X11 integration tests on KWin 6.6, 2026-09-19

The two tests added with the per-application X11 rules,
`repeatedFullscreenTransitions` and `independentOutputRules`, failed the
nightly's Ubuntu 26.04 package jobs. They had been validated on Trixie's KWin
6.3.6 and on neon, whose eleven available entries exclude the integration
tests because they only register below KWin 6.7. Ubuntu 26.04 carries 6.6.6,
where they register and run. Both failures were in the tests, not the plugin.

`repeatedFullscreenTransitions` asserted the substring
`"true true QRectF(0,0 3840x2160)"` against the driver's window dump. Those two
booleans are `isNormalWindow()` and `noBorder()`, and the test is about neither:
its own comment is about KWin updating the logical frame to cover the output
while the native configure is blocked. On 6.6.6 the window reports `true false`
with the frame geometry already correct, so the assertion was pinning KWin's
decoration state by accident. The driver now labels the geometries it prints
and the test anchors on `frame QRectF(0,0 3840x2160)`. Labelling was needed
because the unlabelled dump is not stable across versions either: `geometryF()`
returns `KWin::RectF` from 6.6 and `QRectF` before it, which is the same
boundary `compatibility.h` already detects as `UPSCALE_REGION_API`.

`independentOutputRules` reconfigured, waited a fixed 500 ms and compared.
Reconfiguring restores every managed window before applying the rules again, so
a window the changed rule does not concern still leaves its reduced mode and has
to return to it, and 500 ms did not cover that on amd64. QtTest reported the
round trip precisely: "8100 ms would have been sufficient this time". The test
now waits for the window with a 15 s bound, and the fixed delay stays only after
it has settled, where it still serves its purpose of catching the changed rule
wrongly resizing the other window.

The 8.1 s is this control's own documented retry path, not a stall. A request is
validated 3 s after it is issued; a failed validation restores and reschedules
250 ms later; that request is validated 3 s later again. About 6.25 s of timers
plus round trips is what was measured.

Open question, not a test defect: on 6.6.6 the request issued after a
reconfiguration's restore does not pass validation and only the retry recovers
it, where 6.3.6 succeeds within the original 500 ms. It self-corrects, but it
leaves the game at the wrong resolution meanwhile.

**How long that is was measured again on 2026-09-20, and once is not enough to
know it.** The nightly's resolute jobs failed on both architectures with the
15 s bound above, and in the same container here QtTest reported that
`lifecycle(secondary-fullscreen)` needed **18350 ms**. So the wait is not the
8.1 s of a single retry: it varies between roughly eight and nineteen seconds
on the same build, which suggests more than one validation failing in a row
rather than a fixed path. Every bound in that case is now 30 s - chosen to
cover the worst seen with room, not to make a number pass - and the README
tells a user up to twenty seconds rather than about eight.

That variability is the part worth investigating: a user changing a preset on
KWin 6.6 waits an unpredictable time, and a retry path whose length depends on
how many validations fail is not something to leave undescribed in the
handbook once it is understood. Not investigated here.

Observed results, both from an isolated copy of the branch head under `build/`
so that another session's concurrent edits could not affect them:

- Ubuntu 26.04, KWin 6.6.6: `upscale-x11-integration` 100% passed, 66.65 s,
  all thirteen cases. Before the fixes, on the same image, `independentOutputRules`
  and `repeatedFullscreenTransitions` failed exactly as the nightly reported.
- Debian Trixie, KWin 6.3.6, warnings as errors: 100% passed, 54.72 s.

`upscale-check:local` on the development machine is stale and has no
`/tmp/.X11-unix`, which `containers/trixie/Containerfile` creates. Every X11
case fails there in about 210 ms with `kwin_xwl: /tmp/.X11-unix does not exist`
before any test logic runs. That is an image to rebuild, not a code failure.

### The arm64 package job refuses a slow machine, and a validation-window fix did not cure it

Where the evidence in this section comes from, because the two are not
interchangeable: the failures below were seen in **hosted** runs of the package
job, one scheduled nightly on `master` and the rest manual verify-only
dispatches of `nightly.yml` on the packaging branch. Everything under
**Reproduction** and **What was tried and rejected** is **local**, in a
container on a developer's machine, and is evidence about the mechanism rather
than about the pipeline.

The hosted `resolute arm64` package job fails one X11 integration case per run,
and never the same one: `repeatedFullscreenTransitions` on 2026-09-19,
`lifecycle(primary-fullscreen)` on 2026-09-20, and `repeatedFullscreenTransitions`
again on the re-run of that same commit. The suite's runtime moved with it -
88.6 s, 61.1 s, then 156.2 s for the identical thirteen cases - which is the
runner's speed rather than the change under test. `resolute amd64` passes.

**Reproduction.** `pipeline-resolute` (Ubuntu 26.04, KWin 6.6.6) with
`--cpus=1` reproduces it exactly: same case, same line 135, same refusal text.
Unconstrained on the same image it passes, which is why amd64 never shows it.
The image needs `/tmp/.X11-unix` created and the `debian/control` build
dependencies installed first; see the note at the end of this section.

**What is established.** `begin()` validates a request three seconds after
making it, and `unmetCondition()` then reads the client's buffer. A client
answers a resize on a frame it is given the chance to draw, and that runner
presents every 3.1 s, so the verdict can be reached before one frame carries
the answer. Every failing run logs the same refusal: "The application
repeatedly replaced its window without accepting the request."

**What was tried and rejected.** Deferring validation while the output had
presented fewer than N frames, bounded by a cap, frame-driven rather than
polled. Three variants were measured under `--cpus=1`, five clean sequential
runs each. All failed, and the failing assertions reported the same
32.5-33.4 s requirement against their 30 s windows whether the cap allowed 33 s
or 15 s - so the deferral was not what determined the outcome. Worse, the
one-frame variant failed 3/3 unconstrained, where the unmodified code passes
100%: the change regressed ordinary hardware and was reverted in full.

**The open lead.** `attempt.count` in `begin()` increments whenever the key's
`X11Window` differs from the last, and `m_attempts` is only cleared by a
successful validation. `repeatedFullscreenTransitions` legitimately replaces
its window eight times; where validations do not succeed promptly the count
passes six and the effect refuses, telling the user their application would not
cooperate when what failed to keep up was the machine. That is the refusal the
nightly logs, and it is untouched by any validation-window change. Not yet
investigated: whether the count should ignore a replacement the effect's own
restore provoked, decay with time, or reset on a deferred request.

**Consequence for the release.** `publish` depends on `package`, and
`tools/release_assets.py` requires the complete distribution x architecture
matrix, so a missing `deliverable-resolute-arm64` blocks the nightly release
entirely. There is no partial-matrix route that does not weaken that check.

Both maintained images are stale against `debian/control`: they predate
`libxcb-randr0-dev` and `libxcb-composite0-dev`, so a container build fails to
configure until the build dependencies are installed, and `pipeline-resolute`
has no `/tmp/.X11-unix`, so every X11 case fails in about 300 ms before any
test logic runs. Rebuilding them is outstanding and is not a code failure.

### What actually cured it: the client committed its buffer too late

The refusal counts window replacements without a successful validation, and the
validation reads the client's buffer. The test client was answering a resize in
the wrong order: on `ConfigureNotify` it called `mode()` first, which is several
synchronous RandR round trips, and only painted afterwards. On a slow machine
those round trips outlast the three-second validation window, so the effect read
the buffer from before the resize, refused the request as "the application
supplied a 3840 x 2160 buffer where 1920 x 1080 was requested", restored, and
retried - which is what drove the replacement count past six and produced the
refusal the nightly logged.

`X11Client::dispatch()` now paints the new size before anything that can block,
and paints again after the emulated mode is established, which is what a client
does anyway. Nothing in the plugin changed: the validation window, the retry and
the replacement counter are untouched.

**Observed, locally.** Ubuntu 26.04 with KWin 6.6.6, the image built from
`containers/package` on a developer's machine, not a hosted runner: before the
change the suite failed two cases, after it thirteen of thirteen pass. Repeated under the documented `--cpus=1`
reproduction, which is the constraint that reproduces the nightly: thirteen of
thirteen pass. Debian Trixie with KWin 6.3.6 natively: thirteen of thirteen
pass, as before, so ordinary hardware is not regressed - which is where the
rejected frame-deferral variant above failed.

This supersedes the timeout raise that was tried first. Raising the bound from
5 s to 30 s moved QTest's own verdict from "8250 ms would have been sufficient"
to "33250 ms would have been sufficient": the same 3250 ms - the validation
window plus the reschedule - measured from whenever the wait gave up. A bound
was never going to reach it.

**Hosted confirmation is outstanding.** No run of the package job has yet
carried this change. Until one has, the cure is established on the mechanism
and on a local reproduction of it, and not on the pipeline that reported the
failure.

### The Kubuntu package job still fails, and could not be reproduced here

The hosted `resolute` package job fails on both architectures on `4ec65c3`,
which carries the buffer-ordering fix, while `trixie` passes on both and all
five distribution-package jobs pass. It is always `upscale-x11-integration`,
and always with QtTest's own verdict rather than a refusal:

    lifecycle(primary-fullscreen) ... the requested timeout (30000 ms) was too
    short, 33050 ms would have been sufficient this time.
    Loc: [./autotests/x11_integration_test.cpp(149)]

That figure is the signature worth keeping. QTRY re-runs its loop for twice the
bound after timing out and reports `timeout + elapsed-in-the-second-loop`, so
33050 against 30000 means the condition became true 3050 ms into the second
loop - and 8250 against 5000 meant 3250 ms into it. Three runs, three bounds,
the same ~3.1 s measured from wherever the first loop happened to stop. A
bound that is simply too small cannot produce that: the second loop polls
identically, so a fixed amount of work would complete at a fixed time.

**Not reproduced here, after four attempts**, all on `containers/package` built
from `ubuntu:26.04`, all passing:

| Attempt | Result |
| --- | --- |
| `upscale-x11-integration` alone | 13 of 13 |
| the same under `--cpus=1` | 13 of 13 |
| the whole suite at `CTEST_PARALLEL_LEVEL=4` | 17 of 17 |
| the same constrained to `--cpus=4`, as the runner has | 17 of 17 |

The package job runs the suite through `dh_auto_test`, which honours
`CTEST_PARALLEL_LEVEL` from `DEB_BUILD_OPTIONS`, so several nested KWin
sessions do share the runner. That was the most promising explanation and it
did not hold here.

**What was done instead of a claimed fix.** The waits that have failed in
hosted runs now report what they saw, what they wanted, and the effect's own
status, through `UPSCALE_TRY_GEOMETRY`. `QTRY_COMPARE` has no message form, so
three hosted failures in a row reported only that a wait expired, and each had
to be guessed at. The next one will say what the effect decided.

### What it was: KWin 6.6 undoes a request the client's withdrawal overtakes, 2026-09-21

Reproduced and fixed. **It was a real defect of the effect on KWin 6.6, not a
timeout and not a slow runner**, and it cannot occur on the minimum supported
KWin, which is why Trixie never showed it.

**The verdict had been misread.** QtTest's "N ms would have been sufficient"
counts the 50 ms steps of its polling loop, not time, and it is also printed
when a condition holds at one step and fails at the next: the first loop ends
early and the report adds the whole bound to the second loop's count. A probe
in the Ubuntu 26.04 image showed a 2000 ms bound at 100 ms per step reporting
"2800" while 5.9 s of wall time passed. So the 5 s, 15 s and 30 s bounds all
reporting "bound plus about 3.1 s" meant **the wait lasted about 3.1 s every
time**, and the hosted amd64 runner was fast, not slow: its whole X11 suite
took 59.5 s while "containing" a 33 s wait that never happened.

**Reproduced from the PR head `f8577f4`** in `localhost/upscale-package:resolute`
(KWin 6.6.6, Xwayland 24.1.10): the first run failed
`lifecycle(primary-fullscreen)` with "33100 ms would have been sufficient", and
`--repeat until-fail:6` failed on the third iteration, both fullscreen rows.

**The mechanism**, from a timestamped effect log with the geometry polled every
10 ms:

    after configure(true)                       geometry 3840x2160
    [+14 ms]   effect requests 1920x1080
    [+17 ms]                                    geometry 1920x1080
    [+35 ms]                                    geometry 3840x2160   <- undone
    [+3.1 s]   validation retry requests 1920x1080
    [+3117 ms]                                  geometry 1920x1080

KWin 6.6's `X11Window::propertyNotifyEvent` answers a PropertyNotify for
`_XWAYLAND_RANDR_EMU_MONITOR_RECTS` - the atom KWin calls
`xwayland_xrandr_emulation` - with `configure(m_bufferGeometry)`, and
`X11Window::configure()` sizes the client to the emulated mode the property
names, or to the whole frame when the property is gone. Present in KWin master
and in 6.6.0, absent in 6.3.6, 6.4.0 and 6.5.0; checked in the sources, not
assumed. Xwayland sets or deletes that property on every RandR mode change. So
the effect's restore sizes the window back to native, the client withdraws its
emulated mode as a real client does, Xwayland deletes the property - and the
effect's next request goes out before KWin has processed the notification.
KWin then sizes the client to the full frame, undoing the request; the client
answers that by withdrawing again, overwriting its own new mode too.
Validation fails three seconds later, the retry restores and re-requests
250 ms after that, and that one holds. The same mechanism explains the 8.1 to
8.3 s this document recorded on 6.6 as an open question - 5 s plus about
3.2 s - and the 18 350 ms case. A 50 ms poll sometimes sampled inside the 18 ms
the first request stood, which is why it only failed sometimes.

**The fix waits for the withdrawal before asking again.** `restore()` notes
whether the client held an emulated mode when the window was handed back and,
if so, the next request waits for the property to change, with a 3000 ms
fallback so that a client which never withdraws still gets its request and
validation says what happened. The event filter takes `XCB_PROPERTY_NOTIFY` for
it and schedules the request after KWin's own handling of the same event. The
test's `UPSCALE_TRY_GEOMETRY` no longer uses a QTRY loop: it polls every 10 ms,
records every geometry it saw, and on failure prints that trace and the
effect's status. No bound was raised, and none is needed.

**Observed on `f8577f4` with the fix**, all in detached containers: on Ubuntu
26.04 with GCC, `ctest -R upscale-x11-integration --repeat until-fail:8` passed
8 of 8, then the whole suite 17 of 17, the X11 suite about 5 s shorter than
before because the retries are gone; on Trixie, GCC and Clang, 17 of 17 each.
The same fix carried onto the settings redesign is verified separately.

**Not verifiable here: the hosted runner itself.** The next nightly or package
run on `resolute` is what confirms it; a failure there now prints the geometry
trace and the effect's status instead of a misleading bound.
