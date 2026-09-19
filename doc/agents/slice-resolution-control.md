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
The earlier [launching proposal](slice-application-launching.md) is superseded.
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

## Remaining work

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
