<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Wayland compatibility and effective scaling

## Start and end state

The existing VM run named Wayland executes Piglit inside nested KWin. Its
outer effect receives a native-size buffer and explicitly refuses to scale.
KWin 6.3.6's own integration tests instead create private compositors, normally
using QPainter, and do not exercise an effect enabled on the surrounding desktop.
Neither arrangement answers whether this effect changes upstream test outcomes
or reliably obtains and scales smaller buffers.

The end state is a reproducible VM comparison of KDE's original integration
test assertions with the effect absent, loaded but unselected, and enabled,
plus explicit production-renderer tests of reduction, scaling and restoration.
Every result must identify whether the effect loaded and whether scaling ran;
missing cases, skips and baseline failures cannot become passes.

## Scope, dependencies and gates

Own the upstream test adapter, runner and comparative reporting. Use the
maintained containers and existing isolated VM with its virtual render device.
Keep all copied upstream sources and generated results under build/. Do not
change the running physical desktop or other sessions' VM runs.

The supported gate covers KWin 6.3.6, native Wayland, SDR, eligible opaque
fullscreen and borderless surfaces, cooperating and ignoring clients, effect
lifecycle and unrelated-window isolation. Compare original upstream assertions
with the same OpenGL backend in every arm. Additional cases must require
committed smaller buffers and successful production FSR draws, not just requests.

Full acceptance retains the handbook's requirements: supported KWin versions,
real applications and graphics APIs, Xwayland/Wine/Proton, mixed outputs and
input, image quality, physical HDR/VRR and hardware acceptance. This VM gate
does not close those requirements. Resolution behavior belongs to
[resolution control](slice-resolution-control.md); physical rendering acceptance
belongs to [FSR rendering](slice-fsr1-hdr-vrr.md).

## Approach and progress

- Prepare a private copy of the packaged KWin source and inject only test
  startup configuration, effect loading and observable engagement checks.
- Build and run the upstream integration tests with identical graphics and
  isolated configuration in all three arms; retain individual QtTest outcomes.
- Add focused native Wayland cases using KWin's real renderer and the production
  effect, covering positive reduction/scaling and negative/restoration behavior.
- Investigate new failures, rerun controls where needed, and report a bounded
  coverage matrix rather than a universal compatibility claim.

2026-09-27: the VM's virtual DRM device supports llvmpipe OpenGL. Built 82
packaged KWin integration executables plus the added production test. The first
complete comparison ran all 246 upstream invocations with confirmed OpenGL
and effect states. The absent arm recorded 1541 passes, 17 failures and 10
skips; the idle arm 1534 passes, 24 failures and 10 skips; the active arm 1491
passes, 67 failures and 10 skips. These are individual QtTest outcomes, including
setup and cleanup, not counts of assertions. Input capture aborted in every
arm. Results remain under `build/wayland-conformance/full-1/`.

Six idle differences are tests that require exactly one effect to be loaded;
they observe two when upscale is present. One Xwayland input timing failure did
not recur in the focused repeat. Active mode deliberately changes advertised
output modes and adds scale-related configure events. Repeated failures also
include X11 initial activation/stacking and an input-method resize expectation;
these remain unresolved compatibility differences, not accepted passes. The
repeat is under `build/wayland-conformance/repeat-1/`.

The added test caught an output lifecycle bug: `screenAdded` may fire before
WaylandServer constructs the protocol output. The effect then misses the
client's bind and never advertises a smaller mode on that output. Watching
`Workspace::outputsChanged` instead catches the complete transaction. The
production test replaces the output before binding a client and verifies that
the advertised size yields a smaller committed buffer and a completed draw.

`smoke-8` passed all 16 QtTest outcomes (14 data cases, setup and cleanup).
Six cases obtain and render 256×144 buffers into a physical 384×216 output at
desktop scales 1, 1.5 and 3, with both fullscreen states checked. Rendered
pixels differ from ordinary KWin enlargement. Advertising after output
replacement, restoring on disable/unload, ignoring an unanswered request,
windowed isolation and native/below-half/wrong-aspect/transparent fallbacks
also pass. Fallback pixels match ordinary KWin. Image comparisons wait beyond
an already pending presentation; earlier captures had observed stale frames.

The first pre-commit stage passed on the changed files. The checker regression
tests passed all five cases. The Trixie GCC production plugin built with
warnings treated as errors. The second stage, Clang and Neon checks and a full
comparison after the output lifecycle fix are in progress. Full hardware and
real-application acceptance remains open.

2026-09-28: the idle differences are all tests asserting that exactly one
effect is loaded; they cannot pass with any second effect. The X11 activation
and stacking differences were the effect's held X11 mapping, proven by a
control build without the hold (34 differences to none) and fixed in
[resolution control](slice-resolution-control.md). Leaving fullscreen with a
server-side decoration returned 498x250 for 500x250: KWin rebuilds the
decoration at the scale the effect asked for, and the effect gave the scale
back only after the client's next commit. The scale now goes back when KWin
requests a geometry that no longer covers the output, in the same configure,
and the decoration is rebuilt at the restored scale.

`tools/check-wayland-conformance.py` now names the cases the supported scope
excludes, with the reason and the arms, and reports them as excluded rather
than dropping them: the six effect-count cases, the output mode told to a
program the effect acts on, and six cases whose undecorated window covering
its output is a borderless presentation, where asking for another scale adds
one configure the test counts. The handbook lists the same cases.

`full-3` (four jobs, all 246 upstream invocations plus the production cases)
had no idle regression, 32 excluded outcomes and the production scaling case
engaged; three active differences remained. They were the scale's timing: the
effect also asked for a scale after KWin had already requested a restored,
decorated window, because the committed buffer still covered the output. The
request and the release now both judge what KWin has requested - a geometry
covering the output, and fullscreen or no scheduled decoration - and the
decoration is rebuilt at a restored scale. What remains of those cases is
the configure a scale change costs: when it goes out depends on the frame at
which the window qualifies, so three more upstream cases that count
configures are excluded by name for that reason.

`full-5`, 2026-09-28, with every change above: all 246 upstream invocations
and the production cases ran; no idle regression, no active difference, 31
outcomes excluded by name, the production scaling case engaged (smaller
buffers committed and drawn at desktop scales 1, 1.5 and 3). Five upstream
tests fail in the baseline; input capture aborts in every arm including the
baseline, which the checker now records as the system's rather than holding
it against the effect. Two reruns of the Xwayland input and colour
management cases that had flipped in `full-4` showed no difference.

2026-09-28, found while running a new production case for
[resolution control](slice-resolution-control.md): the harness does not build
from the committed tree. `autotests/kwin_conformance.h` connects to
`EffectsHandler::effectsChanged`, which KWin 6.3.6 declares as a protected
member function, not a signal, so `kwin_wayland_test.cpp` fails to compile.
That run removed the connection in its disposable prepared copy only.

Fixed the same day: the harness now connects to the effect's own `destroyed`,
which KWin emits on every version when it unloads an effect. Built from the
committed tree and run in the VM: `testUpscaleProduction` passed all 17
outcomes; `testDontCrashReinitializeCompositor`, which reinitialises the
compositor, is reported with the effect lost in the idle and active arms and
not in the absent one, and so as invalid there; `testBounceKeys`, which does
not touch effects, is never reported lost. Two more notes for the next
build: `build/kwin-6.3.6` is the bare upstream source, which asks for
KDecoration and Plasma 6.3.6 and does not configure on Trixie; Debian's
packaged source, whose `relax-interplasma-versioned-deps.patch` lowers that to
6.3.4, does. And the earlier builds under `build/wayland-conformance/` were
gone and had to be made again.
