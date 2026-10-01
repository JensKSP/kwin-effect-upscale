<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Open items across the slice documents

Temporary working list, started 2026-09-28 and committed on Jens's request of
the same day, until the items are worked through. Each item points at the slice
document that owns it; results and decisions go back into that slice, and this
list is deleted once it is empty. Line numbers are as read on 2026-09-28.

Type: **fix**, **impl**ementation, **test**, **decide** (Jens),
**investigate**, **doc**. Gate: **S** supported scope, **F** full acceptance,
**–** none stated. Status: open unless marked.

## A. Broken right now (in no slice document)

- **1.** **fix, S** – Kubuntu 26.04 package build fails on amd64 and arm64 in
  today's nightly at `ee6b3b2`:
  `src/plugins/upscale/x11resolution_present.cpp:63`, no
  `KWin::Region(const QRectF&)` on KWin 6.6. Diagnosis: `SurfaceInterface::input()`
  is `QRegion` on 6.3, integer `Region` on 6.6, `RegionF` on master (neon), so
  only 6.6 falls into the branch that builds a region from a `QRectF`.
  Proposed fix: integer regions compare against
  `UpscaleRect(buffer.toAlignedRect())` as 6.3 already does, `RegionF` keeps
  the exact rectangle. **Closed: `cdde4a9`, pushed; the verify-only nightly
  36410753240 built Kubuntu 26.04 on both architectures.** Also fixes the same break in `autotests/effect_driver.cpp:312`.
  Built with warnings as errors on Trixie gcc/clang (tests pass), neon
  gcc/clang, KWin 6.6.6 (every target); tidy and both pre-commit stages pass.
- **1a.** **investigate, S** – Nine X11 test cases fail on KWin 6.6.6 / Xwayland
  **Done with item 6, `7547788`.**
  24.1.10 (Kubuntu 26.04 image `localhost/upscale-package:resolute-kwin66`),
  identically with and without the fix above:
  `keepsEmulatedPointerCoverage` (primary, secondary): input region is
  3840×2160, test expects Xwayland 24.1.6's 1920×1080;
  `withdrawnWhileHeld`: no UnmapNotify (test added in `c7020b8`);
  `defersWineUntilPrepared` (6 rows): helper conversation. The nightly does
  not run these on 6.6, so nothing gates them. Owner: resolution control
  (see item 6). Logs: `build/fix-kwin66/logs3/`.
  Cause of the six Wine rows confirmed: the stand-in is a renamed copy of
  `sleep`, and Ubuntu 26.04's uutils `sleep` is a multi-call binary that
  exits at once under another name. Proposed, not approved: a test-built
  stand-in that blocks on stdin, used by both tests that start `sleep`.
- **1b.** **investigate/impl** – Avoid sleeping in tests where possible: 49 fixed
  waits (`QTest::qWait`, `time.sleep`) in `autotests/` and `tools/`, most in
  `x11_integration_test.cpp` (11) and `x11_prepared_test.cpp` (6). Some can
  wait on a condition; the "nothing happens within X" ones need a signal.
  Production code (`src/`) has none.
  **Agreed with Jens 2026-09-28:** go through all of them; waits for something
  to happen become condition waits; waits proving something does not happen
  get a signal where cheap, otherwise stay bounded with a comment saying why.
  **Done 2026-09-28, except the legacy helper path:** 24 waits replaced. The
  effect counts its X11 validations (`judgements()`), the Wayland client its
  presented frames, the X11 client gained a round trip; the rule is in
  `doc/conventions.md`. Left as they are: polling loops and retry backoff in
  `tools/`, measurement durations in `measure-frame-times.py`, two bounded
  waits with no signal (commented), the 300 ms in `x11proxy_session_test.cpp`
  that belongs to item 26, and the 13 in the Wine helper, prepared-list and
  prepared-session tests, which wait for item 32. Committed as `c24ca56`; the
  300 ms went with item 26 (`d068860`). The 13 went with the companion's tests
  on 2026-09-29 (item 32); the Wine guard's own case that replaced them waits
  for the status reason instead of 3.5 seconds.
- **2.** **test, S** – Nightly has published nothing since 2026-09-20 (`c88f842`):
  red 23–27 Sep on `Build / FreeBSD amd64 Package` at `94d93e0` (fixed by
  `ee6b3b2`), then item 1. The release carries none of the stable download
  names, so the README's Downloads links resolve to nothing. Needs a green
  nightly after item 1. **Verified: verify-only nightly
  [36410753240](https://github.com/JensKSP/kwin-effect-upscale/actions/runs/36410753240)
  on `release/0.3.0` at `1bd904e` passed every job** (all package builds and
  tests, neon, FreeBSD, attestation). Nothing published; the scheduled master
  nightly stays red until `release/0.3.0` reaches master.
  **Agreed with Jens 2026-09-28:** waits for #23. After the merge, check that
  the next scheduled nightly publishes, that the release carries the stable
  download names, and that the README's links resolve.

- **2a.** **fix** – `autotests/kwin_conformance.h:65` (from `ee6b3b2`, #21) connects
  to `EffectsHandler::effectsChanged`, which is a protected member function,
  not a signal, in KWin 6.3.6 (Debian's packaged source). The VM harness
  therefore does not compile against the KWin it is for; no VM run of
  `check-wayland-conformance.py` can build from the committed tree. Found
  2026-09-28 while building item 3's VM case; worked around only in the
  disposable prepared copy under `build/wayland-conformance/source`. Owner:
  Wayland conformance slice. Also: `build/kwin-6.3.6` is the bare upstream
  source and does not configure on Trixie; the packaged source with Debian's
  `relax-interplasma-versioned-deps.patch` is in `build/kwin-packaged/`.
  Recorded in the Wayland conformance slice.
  **Closed 2026-09-28, `94cbb30`, CI run 36442595501 green**: connects to the effect's `destroyed`;
  VM run from the committed tree shows the unload detected in the idle and
  active arms of `testDontCrashReinitializeCompositor`, not in `testBounceKeys`.

- **2b.** **impl, decide** – Recreate the conformance VM from the repository (Jens,
  2026-09-28: a fresh VM for each big release). Today it exists only as files
  under the ignored `build/release-conformance/vm/` (cloud-init user-data and
  meta-data, `start.sh`, keys, an image tarball), and the host image
  `localhost/upscale-vm-host:trixie` has no Containerfile in the tree. Needs:
  a Containerfile for the VM host, a cloud-init template, a script to create,
  start, stop and run commands in the guest (generating keys and known_hosts,
  loading the test image), the KWin harness build steps (packaged source), and
  documentation in `doc/checks.md`. Owning slice to decide: Wayland conformance
  or build and release pipeline.
  **Agreed with Jens 2026-09-28:** `containers/vm-host/Containerfile`, a
  cloud-init template beside it (no key in it), `tools/conformance-vm.py`
  (create with a fresh key and known_hosts, start, stop, run in the guest's
  test container, prepare KWin's packaged source), and when and how in
  `doc/checks.md`. Owned by the Wayland conformance slice. Checked by
  recreating the VM with the script and running `testUpscaleProduction`.
  **Done 2026-09-29, `0eaffbc`, pushed:** `tools/conformance-vm.py` with `containers/vm-host`;
  a machine recreated with `create --replace` ran `testUpscaleProduction`, 17
  of 17. Recorded in the slice and in `doc/checks.md`.

- **2c.** **Closed 2026-09-28, no defect.** Locally, under the address sanitizer,
  every session reported an 8-byte leak inside KF6ConfigCore in the KWin
  process, also on `HEAD`. It was the local invocation: CI runs the sessions
  through `tools/run-render-tests.py`, whose `fast_unwind_on_malloc=0` records
  whole allocation stacks, which the existing KF6 suppressions then match.
  Run with those options, `HEAD`'s Wayland session passes without a report.
- **2d.** **investigate** – On KWin 6.6 an X11 window's PID comes from XRes (the
  connection's owner), no longer from `_NET_WM_PID`. The effect's map hold
  (`holdMap`) still reads `_NET_WM_PID`, its window identity `window->pid()`;
  and behind the X11 session proxy the connection's owner is the proxy.
  Whether identification on 6.6 through the proxy still finds the game is
  untested. Found 2026-09-28 during item 6. Partly answered by the slice:
  the proxy restores XRes QueryClientIds results from the original peer PIDs,
  so KWin 6.6 sees the game rather than the proxy. The effect's two PID
  sources remain.
  **Fixed 2026-09-28:** the map hold asks XRes where `_NET_WM_PID` is unset;
  `anUnnamedProgramIsHeldAtItsFirstMapping` fails without it on KWin 6.6 (3 of
  3) and passes with it (3 of 3). Committed as `a7e18d5`, pushed; the hosted
  run on KWin 6.6 is the first nightly after the merge.
- **2e.** **upstream?** – KWin 6.6.6 / Xwayland 24.1.10 leaves a window its client
  withdraws right after mapping it mapped, and the client hears no
  UnmapNotify, with or without the effect; KWin 6.3.6 withdraws it. KWin
  6.3.6 also leaks a QPointingDevice and an OffscreenQuickView per crashing
  X11 client (suppressed in `autotests/lsan.supp`). Both observed 2026-09-28.
  Reproducer (no effect, bare nested KWin): 6.3.6 withdraws the window 3 of 3;
  6.6.6 and master leave it mapped 3 of 3, with Xwayland 24.1.10 and 24.1.8.
  **Agreed with Jens 2026-09-28:** (a) report to KDE, later: prepare the
  report text with the reproducer then; filing is Jens's call. (b) not reported,
  stays suppressed and documented.
  **Report text prepared 2026-09-29** in the resolution-control slice, with the
  measured versions and the reproducer's command; filing is Jens's.

- **2f.** **impl, test** – Install every package in a standard installation of
  every system it is built for, and show the effect works without further
  configuration (Jens, 2026-09-28). **Agreed with Jens 2026-09-28:** one VM per
  system and architecture (Debian Trixie, Kubuntu 26.04, Fedora, openSUSE
  Tumbleweed on amd64 and arm64, Arch amd64, FreeBSD amd64): the system's cloud
  image with its usual Plasma desktop and SDDM; OpenGL by Mesa llvmpipe in the
  guest on virtio-gpu, never the host GPU; amd64 under KVM, arm64 under full
  system emulation (qemu-system-aarch64) before releases, binfmt not enough
  because it boots no system; check whether GitHub's arm64 runners offer KVM.
  In each: install the package with the system's package manager, log in
  through SDDM into Plasma Wayland, and check KWin running, the effect loaded
  and supported, the settings module, the proxy, a smaller buffer scaled, an
  X11 program answered through the proxy, removal leaving KWin running; one
  report per system. Tooling in Python under `tools/`, the best fit for the
  repository's own tools. 2b becomes one profile of it, and it tests 8 on every
  distribution. Owned by a new slice for package installation in standard VMs.
  **Supported scope done 2026-09-29:** `tools/package-vm.py` and
  `tools/package_check.py`; a fresh Debian Trixie amd64 machine with Plasma
  passed all nine steps with the nightly's package, SuperTuxKart enlarged from
  2560 × 1440 and Extreme Tux Racer answered by the proxy. The other systems
  remain. Owned by the package machines slice.
  **Kubuntu 26.04 amd64 done 2026-09-29:** its first run found a defect: at
  the scale 2.7 Plasma 6.6 chose, a fullscreen window one device pixel short of
  its output, which the effect allows, came out a hair over one pixel and was
  refused, and so at many common scales. Fixed with a millionth of a pixel of
  slack and a unit check; with a package of the fixed tree all nine steps
  passed. Found on the way: the tests do not build with GCC 14 at -O2
  (`-Warray-bounds` in the proxy session test), fixed. Fedora, openSUSE, Arch,
  FreeBSD and arm64 remain.
  **Fedora, openSUSE and Arch machines made 2026-09-29;** their checks wait for
  a nightly package of the current tree. **arm64 profiles added the same day**
  for Debian, Kubuntu, Fedora and openSUSE (Arch publishes no arm64 image),
  emulated whole with virtio-gpu for the screen; not yet booted. **The Debian
  arm64 machine finished its first boot the same day** in 38 minutes, logged
  in to Plasma's Wayland session on aarch64 with virtio-gpu offering
  3840 × 2160; its package check waits for the nightly's arm64 package.
  **Fedora 43, openSUSE Tumbleweed and Arch amd64 passed 2026-09-29,** all
  nine steps each, with nightly 36545995686's packages of `7ff0fe6`:
  SuperTuxKart enlarged from 2560 × 1440 on Wayland in each, and an X11
  game answered by the proxy (Extreme Tux Racer on Fedora, SuperTuxKart
  where Extreme Tux Racer is not installed).
  **Debian arm64 passed 2026-09-29,** all nine steps, emulated whole, with
  the same nightly's arm64 package, at the second run. The first failed step
  8: the proxy asked the effect about Extreme Tux Racer's connection while
  the emulated KWin was still busy with the Wayland game killed a moment
  before, the answer took longer than the half second the proxy holds a
  program (`NoReply`), and the game was passed on unanswered; the effect then
  resized its window instead. The check now starts the X11 game from an idle
  KWin, as a player's desktop is, and the second run passed. The other arm64
  machines remain to be made.
  **FreeBSD blocked, for Jens to decide (found 2026-09-29):** a Plasma Wayland
  session needs a DRM/KMS driver, and FreeBSD's drm-kmod drives Intel, AMD and
  NVIDIA hardware only. No QEMU display device has one: virtio-gpu KMS for
  FreeBSD exists only as an open pull request (freebsd/drm-kmod#499, opened
  2026-08-26, aimed at FreeBSD 15.1, still in review). A standard FreeBSD
  installation in a machine can therefore not start KWin's Wayland session.
  Options: (a) FreeBSD keeps the nightly's install, load and removal test until
  the driver ships, and its session check waits for it; (b) the session check
  runs on real hardware with an AMD or Intel GPU, by hand; (c) build that pull
  request's module in the machine, which is no longer a standard installation.
  **Decided by Jens 2026-09-29: (a).** FreeBSD keeps the nightly's install,
  load and removal test in an emptied machine; its session check waits for the
  virtio-gpu driver and stays open in the package slice.
  **Kubuntu 26.04, Fedora 43 and openSUSE Tumbleweed arm64 passed
  2026-09-29,** each machine made and checked under emulation, all nine steps,
  with nightly 36545995686's arm64 packages. Every system and architecture but
  FreeBSD has passed.

- **2g.** **fix, S** – CodeRabbit asked for changes on #23 (review 5342421790
  at `74de7c8`), found 2026-09-28: five comments, each checked against the
  code and holding.

  - `src/x11proxy/display.cpp:165`: the monitor reply withdraws the policy
    only for more than one monitor, not for one monitor spanning several
    outputs, which the resources reply refuses; a client that asks for
    monitors first gets rewritten sizes.
  - `src/x11proxy/display.cpp:174`: `outputInfo()` filters by the modes it
    knows; when the backend no longer offers them, the reply keeps no mode and
    the policy stays on.
  - `tools/check-presentations.py:162`: an explicitly requested case whose
    program is missing reports `absent` and exits 0.
  - `tools/check-presentations.py:275`: `--acted` defaults to true even for
    `--presentation windowed`, which the effect never acts on.
  - `tools/measure-frame-times.py:452`: a missing program fails only after
    kwinrc has been changed, with a traceback and no report.

  **Fixed 2026-09-28, `a64aed9` and `d73802c`, pushed:** all five, the proxy with two unit
  cases in `x11proxy_display_test.cpp`, the tools with
  `tools/test_check_presentations.py` and a case in
  `test_measure_frame_times.py`; each new tool test failed on the old code.
- **95.** **fix, S** – Found 2026-09-30: the first nightly after #23's merge,
    36697907408 on `312c07c`, failed the Arch, Fedora, openSUSE and FreeBSD
    packages with "marked 'override', but does not override": the window drawn
    over its output (item 18) overrides `prePaintScreen()` and took callbacks
    that return void to mean the signature with a presentation time, which
    KWin 6.7 dropped while its callbacks still return void. The `v0.3.0`
    release run 36913783473 of 2026-10-01 failed the same way and published
    nothing.
  **Fixed 2026-09-30:** a second probe beside the return type's asks KWin's
  header whether `prePaintScreen()` takes a presentation time; yes on 6.3.6
  and 6.6.6, no on 6.7.5 and master. Built with warnings as errors against all
  four, the 6.7.5 one in the Arch package image as its recipe builds.

## B. Resolution control – `slice-resolution-control.md`

- **3.** **test, S** – Restoration when an output is unplugged while an override is
  in force, and when the client exits before restoration (L648-669).
  **Closed 2026-09-28, `6a0632d`, CI run 36442595501 green**: three nested-session cases in
  `autotests/integration_output_test.cpp`, new session
  `upscale-integration-outputs`, and the VM case `unpluggedOutputIsPassedOver`;
  each shown to fail against an effect broken on purpose.
- **4.** **test, S** – Scale path for pre-v2 `wl_output` (L648-669).
  **Closed 2026-09-28** with item 3 (`anOutputVersionWithoutScaleIsLeftAlone`).
- **5.** **test, S** – A game that never exits: kill before first commit, kill during
  **Done 2026-09-28, `57d491a`, pushed; CI pending.** Crash tests with a real
  game (glmark2), records accessor, and the growing-record defect fixed.
  an X11 resize, kill/relaunch cycles with memory watched (L1376-1380).
- **6.** **test, S** – Hosted confirmation of the KWin 6.6 withdrawal fix on the
  **Done 2026-09-28, `7547788`, pushed** (with 1a): nightly job on Kubuntu
  26.04 / KWin 6.6; X11 tests pass there locally. Hosted: verify-only
  nightly 36456685843 running.
  resolute runners; X11 test duration on CI (L1956-1966, L2530). Blocked by 1.
- **6a.** **investigate/fix, S** – First hosted run of the new KWin 6.6 job
  (verify-only nightly 36456685843 at `7eac56e`): Kubuntu 26.04 arm64 passed,
  amd64 failed one case, `repeatedFullscreenTransitions`
  (`x11_integration_test.cpp:403`). On one re-entry into fullscreen the effect
  sent its request while the window's frame was still 1920 × 1080, Xwayland's
  emulated mode then dropped, the client drew 3840 × 2160 and validation judged
  the request failed after its three seconds; the one retry crossed the same
  way, and the effect gave up, leaving the window unscaled. The session took
  154 s on the runner against 62 s locally, where the case passes. Open whether
  the test paces badly or the effect requests against a frame that is still
  changing, which a slow real machine could hit too. Found 2026-09-28.
  **Fixed 2026-09-28, `7b3fbf1`, pushed:** `fullscreenRequest()` began a request
  while the client still owed the withdrawal of its previous mode; it now
  leaves that to `apply()`. New case `reenteringFullscreenAtOnce`: 8/8 failed
  on KWin 6.6.6 before, 8/8 passed after. Written up in the slice.
- **7.** **test, S** – Normal launch: re-enable from a session started directly;
  **Done 2026-09-28, `7eac56e`, pushed; CI pending.** (a) covered by
  `proxyRestartStatus`; (b) new `effectSwitchedOffMidSession`.
  disabling stops upscaling at once (L3629-3633).
- **8.** **test, S** – systemd-managed Plasma login path (L3487).
  **Agreed with Jens 2026-09-28:** make it work through each distribution's
  default path (SDDM, Plasma with KWin as a systemd user service; startplasma
  without systemd on the BSDs), not through anything particular to wzpc. Read
  startplasma in Plasma 6.3 and 6.6 for where `plasma-workspace/env` runs and
  how its environment reaches the service; any gap is fixed with a standard
  mechanism. Tested for real by 2f.
  **Done 2026-09-28:** plasma-workspace 6.3.6 and 6.6.6 source the env scripts,
  then hand the environment to systemd before the session starts; wzpc's
  `plasma-kwin_wayland.service` journal shows the proxy starting at three
  logins. Written up in the slice; other distributions with 2f.
- **9.** **test, S** – Neon runtime session, needs a render device (L3479).
  **Closed with Jens 2026-09-28, superseded:** KWin master stays build-only by
  design (`doc/checks.md`); the nightly runs every test on KWin 6.6 (item 6), and
  2f runs the packages in real sessions. A neon VM profile in 2f can add master
  later if wanted.
- **10.** **test, S** – Signal/shutdown review, connection and descriptor-leak
    stress (L3523-3525).
  **Agreed with Jens 2026-09-28, important:** two proxy session tests, run in
  every pull request and under the sanitizers: SIGTERM to a session with an open
  connection ends Xwayland and itself within a bound and removes its socket, and
  an Xwayland that ignores SIGTERM is killed after the grace period; 500
  connections opened and closed leave the proxy's open descriptors as they were,
  counted portably. Plus a short review of the signal path, recorded in the slice.
  **Done 2026-09-28:** `x11proxy_shutdown_test.cpp`, three cases, each failing
  against a session broken on purpose; the review found repeated signals
  postponing the kill of an Xwayland that ignores SIGTERM, fixed in
  `session.cpp`. Passes with GCC and under both sanitizers. Committed as
  `4928f3a`, pushed.
- **11.** **decide, –** – Is the XTS release-conformance gate judged with the suite's
    windows kept from the window manager? (L3936-3939)
  **Decided by Jens 2026-09-28:** the XTS release gate is judged with the
  suite's windows kept from the window manager (override redirect). The run with
  KWin managing them is reported beside it for information, its known
  window-manager races named; a new failure there beyond them is still looked at.
- **12.** **test, F** – Live L4D2 at 4K with mouse look after the `updateShape()` fix;
    last attempt blocked, KWin reported zero screens (L3781-3797).
  **Closed 2026-09-28 on Jens's report:** L4D2 live at 4K, mouse look
  included, tested by Jens and OK. His report, not a recorded measurement; into
  the resolution-control slice with the next commit.
- **13.** **decide → impl, F** – Early Wine/Proton identity: Wreckfest starts at 4K
    unidentified; reading `SteamAppId` needs an exception to "no `/proc`"
    (L3752-3755, L3815-3818).
  **Closed with Jens 2026-09-28, superseded** by the route decided on
  2026-09-25 (Proton slice): the prefix is the unit and the game is recognized by
  the program path Wine names; no SteamAppId, no new `/proc` exception. Wreckfest
  at 4K is solved through items 26 to 28.
- **14.** **investigate, F** – Wine OpenGL fullscreen grows by 1280×720 on every size
    notification; input check never reached (L3861-3869).
  **Agreed with Jens 2026-09-28:** reproduce with the current code first (the
  same Wine OpenGL probe, fullscreen through the proxy, in the conformance VM);
  if it no longer grows, record and close. Otherwise trace which of effect, proxy
  and Wine drives the loop, fix it there, and add a regression test (a Wine
  OpenGL fullscreen window keeps its size over many size notifications, input
  landing). Together with 26 to 28, which share the path and the setup.
  **Closed 2026-09-29:** it no longer grows. A probe written again for
  it, under Wine 10.0 through the proxy at 3840 × 2160 and scale 3, kept its
  2560 × 1440 window for 60 seconds while the effect presented it over the
  output. Input landing stays with 29. Recorded in the slice.
- **14a.** **investigate** – Found 2026-09-29 while measuring 27: with two
  fullscreen Wine programs in one prefix, each setting 2560 × 1440 with
  ChangeDisplaySettings - an unlisted copy of the probe first, the listed
  probe 20 s later - the first one's window grows by 1280 × 720 on every size
  notification up to X's limit, and a Wine process then aborts in libxcb
  (`xcb_conn.c:323: write_vec: Assertion 'n == 0'`). Seen before and after
  27's change, with the proxy and not without it, while the effect acts on
  neither window. Real launchers are not fullscreen programs; still to trace
  what in the proxy's answers drives it.
  **Traced 2026-09-29, a decision for Jens:** Wine 10.0's win32u
  (`map_window_rects_virt_to_raw()`) gives a window whose visible part
  covers its monitor the whole raw monitor as its visible part. Once the
  proxy shows the prefix the game's 2560 × 1440, the launcher's raw monitor is
  2560 × 1440, but KWin keeps its fullscreen window at the output's real
  3840 × 2160, as it keeps every fullscreen window it was not asked to size
  otherwise. Wine asks for 2560 × 1440, KWin answers 3840 × 2160, and Wine
  takes the window as the X size plus the difference between its window and
  its visible part (`window_rect_from_visible()`): 5120 × 2880, and 1280 × 720
  more at every answer (traced with `+x11drv,+win,+system` in the
  conformance machine). The proxy's view and the launcher's window disagree;
  only the selected game's window is sized to the smaller screen. Either the
  prefix's other fullscreen windows are presented at the game's screen too,
  enlarged by the effect although no entry lists them, or connections with a
  fullscreen window are not shown the smaller screen. For the discussion at
  the end, with 18 to 20a.
  **Decided by Jens 2026-09-29:** the prefix is one screen: every fullscreen
  window of a prefix whose game is selected is sized to the game's screen and
  presented by the effect, although no entry lists it. Now an implementation
  item.
  **Done 2026-09-29:** the proxy keeps the screen a prefix shows since its
  game was answered, shows it to the prefix's later connections too, and
  reports each process it shows it to over D-Bus before switching one that
  already runs; the effect claims such a process's windows with the entry that
  answered the game, and looks at them at once. Tests: the proxy session's
  warm prefix reports the launcher and a later program (each failed with its
  part taken out), and `presentsAProcessShownItsGamesScreen` in the
  single-screen X11 session presents a stand-in launcher at 1920 × 1080 (it
  failed without the effect's fallback). In the conformance machine the
  launcher beside the probe is now 2560 × 1440 and presented, where it grew to
  5120 × 2880. Recorded in the Proton slice and the handbook.
- **15.** **fix, –** – Incoherent advertisement: `wl_output.mode` falsified beside a
    truthful `xdg_output`; recorded as a defect in shipped code, no fix
    recorded (L1363-1365, L1841-1851).
  **Agreed with Jens 2026-09-28:** the fix is to make `xdg_output` consistent
  with the told mode (logical size = mode ÷ scale), sent when the program asks
  for its `xdg_output` and given back on restore. To find out first whether the
  effect can reach one program's `xdg_output` on KWin 6.3.6 and 6.6. If stuck,
  call in Fable.
  **Done 2026-09-28, `940b940`, pushed:** KWin offers no hook, so the effect
  notices a told program's `xdg_output` objects through libwayland's public API
  and sends the told logical size after KWin's, and KWin's back on restore;
  both sessions test it. The lasting fix is a `bound` signal on KWin's
  `xdg_output` global, for the KDE report. Recorded in the slice.
- **16.** **impl/test, –** – SuperTuxKart hard requirement: automated matrix of all
    six cells with output capture; live in-game changes (L1233, L1321-1325).
  **Agreed with Jens 2026-09-28:** `tools/check-supertuxkart.py` runs the six
  cells in the 2f/2b VM (llvmpipe OpenGL, lavapipe Vulkan, no host GPU): per cell
  a nested KWin with OpenGL and the installed effect, the test's own game list
  loaded through the settings import, STK with a fresh private configuration,
  and checks of the committed 2560 × 1440 buffer, FSR, and an output capture
  against plain enlargement. In-game changes automated where STK can be driven,
  otherwise on the hardware session's checklist. A report per run, before every
  release. Release gate (hard requirement).
  **Done 2026-09-29 except in-game changes, `39e2bcd`, pushed:** `tools/check-supertuxkart.py`
  with `containers/game-tests`; all six cells pass in the machine, each
  2560 × 1440 drawn to 3840 × 2160 and 25 to 29 % sharper than KWin's plain
  stretch of the same stopped frame. Changes inside the running game stay with
  the checks in Jens's session. Recorded in the slice and the handbook.
- **17.** **investigate, –** – SuperTuxKart writes the reduced mode into its own
    configuration; to be answered from the compositor side (L1256-1264).
  **Answered 2026-09-28, no code change:** Native enlarges a kept smaller
  buffer with FSR; under other presets the display marks a kept size in the
  warning colour. Wording asking the player to change it goes with 43/44.
- **18.** **impl, –** – A program that sizes a plain window from the mode it was told
    has to be presented over its screen (L1345-1347).
  Needs Jens first (moved to the decisions, 2026-09-29): no program known to do
  this has been named, and the mechanism is a choice. Only AdvertisedMode
  leaves such a window smaller than the screen, because the methods that send a
  scale keep mode ÷ scale at KWin's logical size. The effect can make the window
  fullscreen, as it does for a prepared X11 window, but KWin then configures it
  at the screen's own logical size, which the program believes larger than its
  screen, so it may draw at full size again. Drawing the small window over the
  screen instead leaves the pointer where KWin thinks the window is. Which one,
  and with which program to test it. The bench (19) found two: GLFW 3.4 with an
  undecorated window at the video mode's size, and Wine's Wayland driver.
  **Decided by Jens 2026-09-29:** the effect draws such a window enlarged over
  its output and maps the pointer over the whole output onto it, claiming
  focus beside the window as it does for the X11 windows it presents; the
  program keeps the size it asked for. Tested with GLFW 3.4 in the Wayland
  session. Now an implementation item.
  **Built 2026-09-29:** drawn over its output with what it covers left out, a
  decoration KWin gave it included; the pointer claimed beside it, motion
  included. GLFW 3.4's undecorated window, which KWin decorated, filled a 4K
  output and saw the pointer at two thirds of its position everywhere, in the
  conformance machine; `drawsAWindowOfTheToldSizeOverItsOutput` covers it in
  the Wayland session. Recorded in the slice.
- **18b.** **impl, –** – Found while building 18, 2026-09-29: over a
    decoration KWin's pointer focus is empty and its decoration filter takes
    the motion. So UpscaleX11Input, which asks whether KWin's focus is above
    the presented window, claims the pointer over a visible dialog's title bar,
    which then cannot be dragged or closed; and over a hidden window's title
    bar beneath a presented game the game gets no motion, and the cursor can
    take that hidden border's shape. The Wayland filter now asks KWin's hover
    window and delivers claimed motion itself; the X11 filter needs the same,
    with a test in a session that has a decoration plugin (the check image has
    only Aurorae, unconfigured).
  **Done 2026-09-29:** the X11 filter asks KWin's hover window whether a
  window lies above the presented one, and delivers a claimed pointer's motion
  itself. `movesThePointerOverAHiddenTitleBar` switches on Aurorae's Plastik
  theme and moves the pointer onto a hidden window's title bar and along it:
  without the change the game stayed at 1302,611 for the move to 1322,611,
  because only the first motion arrives, with the filter's re-entry; with it
  both arrive. On Kubuntu 26.04, whose KWin comes without Aurorae, the case
  skips. The half about a visible dialog's title bar has no test: no session
  here can present a game with a decorated window stacked above it.
- **19.** **test, –** – The seven-item Auto bench was never run (L1853-1886).
  **Done 2026-09-29, in the conformance machine:** the surface scale reaches
  GLFW 3.4, Godot 4.7 and SDL 3 with high pixel density, pointer landing where
  it looks; Qt, vkmark and SDL 3 without it are reached by neither lever; Wine's
  Wayland driver sizes its window from the told mode and draws into a
  subsurface. Three defects found and fixed, each with a test that failed
  before: SDL 3.2.10 crashed (SIGFPE) whenever it was told a mode at scale 1,
  because the told `xdg_output` size came after a done of its own; the report
  asked a player to choose the size in the game for a program Auto had reached
  under All applications; and a surface scale standing when the output's scale
  changed was asked and given back at the old scale. Recorded in the slice.
- **19a.** **decide** – Whether Auto on Wayland enters the supported scope,
  which the handbook left to the bench; the bench is now run (19).
  Jens, 2026-09-29: Auto is not reached; to be discussed in detail later.
  **Decided by Jens 2026-09-29:** Wayland Auto enters the supported scope for
  the programs that follow either lever: SDL's exclusive fullscreen through
  the told mode, GLFW, Godot and SDL 3 with high pixel density through the
  surface scale. The handbook names what follows neither and how the status
  reports it, and the bench cases are the scope's acceptance.
  **Done 2026-09-29:** the handbook's Auto row states the scope, names Qt,
  vkmark and SDL 3 without high pixel density as outside it with what the
  status shows for them, and makes the bench run its acceptance.
- **20.** **investigate, F** – Six source-led investigations unticked: glmark2 X11,
    integer-scale reachability, ETR secondary output, SuperTux borderless and
    render cost, Wine/Proton D3D presentation (L1546-1634).
  **Run 2026-09-29 in the conformance machine, except SuperTux's rendering cost
  (needs a GPU) and official Proton (item 36):** reachable sizes at scales 1.5,
  2 and 3 reproduced; Extreme Tux Racer stays on the primary output and refuses
  an odd size truthfully; a Direct3D 11 sample under Wine is presented whole.
  Two defects fixed with tests that failed before: the report named a reachable
  step as a wish for the next start, and a Wine program started by its Unix
  path was never identified by the proxy. One finding for Jens: 20a.
- **20a.** **decide** – Under All applications, Auto's X11 resize reaches
  programs that keep the viewport they started with, glmark2 2023.01 and
  SuperTux 0.6.3, and the screen shows the bottom left two thirds of their
  picture enlarged while the report says it succeeded; the effect cannot see a
  viewport. Keep the resize for unlisted programs and document the limit, tell
  unlisted X11 programs the smaller screen through the proxy at connection as
  measured entries are, or leave unlisted X11 programs alone.
  Jens, 2026-09-29: to be discussed with the other cases that do not work as
  wanted once the list is worked down; every one of them has to be solved.
  **Decided by Jens 2026-09-29:** under All applications, the proxy tells
  every unlisted X11 program the smaller screen when it connects, as it does
  for measured entries, and the window resize follows as now. Now an
  implementation item.
  **Done 2026-09-29:** the effect's connection policy answers for a program
  no entry names with the global profile while All applications is on, and a
  Wine prefix's components wait for its program as for an entry's.
  `answersUnlistedProgramsUnderAllApplications` in the single-screen X11
  session failed against the old policy. Measured in the conformance machine:
  glmark2 2023.01 reported a 2560 × 1440 surface and SuperTux 0.6.3's title
  screen was presented whole and centred, where both had shown the bottom left
  two thirds of their picture. Recorded in the resolution-control slice and the
  handbook.
- **21.** **decide, –** – `UserConfigured` method (L2253) versus the later "On/Off
    per presentation" plan (L3586-3588): which one stands.
  **Decided by Jens 2026-09-29:** On/Off per presentation stands and
  `UserConfigured` is dropped: a slot set to Off sends nothing, a smaller buffer
  the player chose is enlarged all the same, and the status already names the
  size to choose. The handbook's section on it and the slice's task go.
- **22.** **test, F** – Physical acceptance: mixed resolutions and scales, output
    movement, input and confinement, TV (L2116-2125).
  Listed as K13 in the [known limitations](slice-known-limitations.md).
- **23.** **impl/test, F** – Proxy: Vulkan and presentation sync, overhead, container
    identity (Flatpak, Snap, Docker), BSD (L3308-3310, L3360-3363, L3487-3489).
  **Partly done 2026-09-29:** the overhead is measured (about 30 microseconds a
  round trip, bulk image data at a third of the rate, drawing unchanged) and
  Vulkan and presentation went through the proxy with lavapipe in the
  SuperTuxKart check. A GPU driver's DRI3 path stays with the hardware checks;
  containers and the BSDs go with 2f and 51. Recorded in the slice.
- **24.** **test, –** – SuperTuxKart on X11 through the proxy never recorded; source
    of Jens's Auto report; ~1 s stalls on 6.6 (L1918-1920, L1950, L3687-3690).

## C. Proton smaller screen – `slice-proton-smaller-screen.md`

- **25.** **doc + decide** – Gates and acceptance criteria still describe the retired
    prefix companion; the proxy route has no gate (L326-352).
  **Decided by Jens 2026-09-29 and done:** the proxy route's gates replace the
  companion's in the Proton slice: Wreckfest under Proton Experimental and one
  Wine game outside Steam, launched normally, for the supported scope; every
  flavour across the matrix on real hardware for full acceptance.
- **26.** **impl, S** – Hold a prefix's first connection until its first real program
    appears, instead of answering within 500 ms (L791-796).
  **Closed 2026-09-28, `d068860`, pushed:** implemented since #21 (held until
  the prefix's program is known, ten seconds at most); the test now waits for
  the session's own message rather than 300 ms. Recorded in the slice.
- **27.** **decide/investigate** – Warm prefix: a running wineserver opens no new
    connection; no answer proposed (L797-798).
  **Decided by Jens 2026-09-29:** the proxy makes Wine re-read: when an entry's
  program appears in a running prefix, the prefix's existing connections are
  answered with the smaller screen from then on and sent a screen-change event,
  so that Wine registers its displays again. Measured first on real games
  (Proton's wineserver lifetime, launcher-first starts). Now an implementation
  item.
  **Implemented and measured 2026-09-29:** measured first in the conformance
  machine with Debian's Wine 10.0 and the OpenGL probe through the proxy: a
  cold prefix gave the probe 2560 × 1440 at start, but after an unlisted
  Windows program or Notepad had opened the prefix it saw 3840 × 2160. The
  session now keeps each prefix's connections and, when a selected program is
  answered with a smaller screen, shows every earlier one that screen and
  sends the RandR events it selected; the probe then saw 2560 × 1440 in both
  warm cases, and explorer re-read its displays at the switch. On the way: a
  process's later connections, one per Wine thread, were never registered with
  their prefix, and explorer's desktop thread is one of them. Proton's
  wineserver lifetime and a real launcher stay with the games (30, 31).
- **28.** **impl/test, S** – The 2026-09-25 decisions: prefix as the unit, path-tail
    matching, launchers, windowed left alone, per-prefix candidate report
    (L764-787).
  **Checked 2026-09-28, `51e56ff`, pushed:** all five implemented since #21. Missing is a
  shipped Wine entry that uses the path, which needs the path Proton reports
  for Wreckfest (item 30). Recorded in the slice.
- **29.** **test, S** – Real Wine input at scale 3 (L748-752, L829-835).
  **Run 2026-09-29: a defect found.** Through the proxy at scale 3 the
  mapping is right until Wine confines the pointer: KWin checks the confinement
  in the surface's unscaled coordinates, so the pointer stays in the top left
  two thirds and the right and bottom third of the game cannot be reached.
  Recorded in the Proton slice.
  **Re-run 2026-09-29 with 29a's interim mapping:** Wine 10.0, confined, reaches
  (2550, 1425) of its 2560 × 1440 window at scale 3, its lower right, which
  before stopped at about two thirds. Input on a real display remains, with
  Jens (supported scope, 94a).
- **29a.** **decide** – How the effect meets a confined pointer until KWin honours
  a presentation transform: map one to one while confined (the system cursor
  is drawn in the wrong place), undo KWin's confinement and clamp in the
  effect (KWin re-engages it), or wait for the KWin change and report it.
  **Decided by Jens 2026-09-29:** propose the KWin change upstream, and map the
  pointer one to one while it is confined on KWin versions without it.
  **The interim half done the same day:** while the presented surface's
  confinement is engaged, `UpscaleX11Input` maps with scale one, relative
  motion included. `aConfinedPointerReachesTheWholeWindow` (scale-3 session)
  confines the pointer as Wine does and receives 1800 × 1050 at that point of
  the output; the old mapping gave 900 × 525. The upstream half is 29b. On
  Kubuntu 26.04 Xwayland locks rather than confines for a window without a
  cursor of its own; the test client sets one, as Wine does, and passes there.
- **29b.** **upstream** – Propose to KWin a per-window presentation transform that
  its input path honours, `Window::mapToLocal()` and the pointer constraint
  checks among it, so that a confined pointer is checked in the picture the
  effect presents. Prepared with the report of 2e; filing is Jens's call.
  **Proposal text prepared 2026-09-29** in the Proton slice, naming the places
  in KWin 6.3.6 that check without the presentation; filing is Jens's.
  The limitation it would remove is K15 in the [known limitations](slice-known-limitations.md).
- **30.** **test, S** – Wreckfest on wzpc through the proxy (L468-472, L925-927).
- **31.** **test, S** – One Wine game outside Steam (L328-330).
- **32.** **decide/impl** – Legacy Wine guards and the Helper1 path: remove or keep
    (L663-671, L744).
  **Decided by Jens 2026-09-29:** remove the companion service, its `Helper1`
  interface, its preparation flow and its tests; keep one rule of the guards, a
  Wine window acted on only through the proxy route, never resized blindly.
  Now an implementation item, with 1b's remaining waits.
  **Done 2026-09-29:** `src/winescreen/`, the effect's helper client,
  preparation, question and prepared-window path, the settings page's Prepared
  Games, the build switch and 14 test files are gone, and with them the
  pointer-event probe and the highlight colour only the question used. The
  guard stays in both places it was (first mapping and every resize request)
  and is now reported in the status: "Wine programs are told a smaller screen
  only by the X11 session proxy, which did not answer this one."
  `leavesWineToTheProxy` in the X11 session covers four Wine loader names and a
  native control, and all four Wine rows failed 3 of 3 with the guard disabled.
  The prepared session's keyboard-focus and pointer-lock cases moved to the X11
  session on the request path; its pointer case duplicated
  `coversPointerWithoutEmulatedMode` and went. The handbook says what a
  development build that enabled the companion leaves behind.
- **33.** **wzpc** – Restore global `OsdStatistics=true` from
    `build/wzpc-clean-start-94a6804/` (L632-636).
- **34.** **test/investigate, F** – Steam as Flatpak and Snap; the 09-27 analysis
    still reasons with the companion (L870-927).
  Listed as K9 in the [known limitations](slice-known-limitations.md).
- **35.** **impl, F** – Present Wine's Wayland driver (L313-319).
  **Reproduced 2026-09-29, not implemented:** under Wine 10.0's Wayland driver the
  window stays at the told 2560 × 1440 and the effect refuses it as not
  covering its output. Needs a Wayland presentation of a smaller fullscreen
  surface with pointer mapping, which shares the confinement gap of 29a.
  Recorded in the Proton slice. The bench (19) adds: the driver draws into a
  subsurface, which the effect refuses even at full size, with buffer heights
  rounded up to a multiple of 128.
  Listed as K3 and K20 in the [known limitations](slice-known-limitations.md).
- **36.** **test, F** – Every flavour × D3D9/11/12/OpenGL/Vulkan × exclusive/borderless
    (L169-174, L335-339).
  Listed as K10 in the [known limitations](slice-known-limitations.md).
- **37.** **doc** – Close the moot helper translation-domain question; owe the
    handbook update in proxy form (L575-579, L592).
  **Closed 2026-09-28, `51e56ff`, pushed:** moot since the companion left the default
  build; the handbook already describes the proxy route. Recorded in the slice.

## D. Application profiles – `slice-application-profiles.md`

- **38.** **test, S** – Trixie Clang and neon GCC/Clang on the tree after the windowed
    slots were removed; only Trixie GCC recorded (L1737-1739).
  **Closed 2026-09-28:** the full check run on `f68eab0`'s tree covered Trixie
  Clang and neon GCC/Clang. Recorded in the slice.
- **39.** **test, S** – Migration from a real configuration written by the previous
    release (L81-82, L915-934).
  **Done 2026-09-28, `af288ce`, pushed:** the 2026-09-20 nightly's own code wrote a
  configuration, kept in `autotests/data/previous-release/`, which the current
  code reads with every value under its current meaning except `OsdPosition`.
- **39a.** **decide** – Where the old single display position goes: the old
  display was one block in one corner, the current one has three blocks with a
  position each, and nothing reads the old key, so a chosen corner falls back
  to the defaults. Which block or blocks inherit it. Found 2026-09-28 with 39.
  **Decided by Jens 2026-09-29:** the statistics block inherits the old
  corner; the other two keep their defaults, and one that would share the
  corner moves on to the next free one, as dragging does. **Done 2026-09-29:**
  `upscaleLegacyCorners()` for the effect and the page, Apply forgets the old
  key; tested with the previous release's configuration, the displaced cases
  and the page, whose case failed against its old loading.
- **40.** **impl, S** – "Clear a profile's overrides" in the editor: required, not in
    the plugin (L60-62, L1244-1246).
  **Done 2026-09-28, `0d3af59`, pushed:** **Use Global Settings** under a game's tabs
  forgets every value it sets and returns its methods to the package's
  measurement. Its label and tooltip are new text for the text review.
- **41.** **decide, S** – What the page shows when a stored `Enabled=false` stops
    every profile (L1380).
  **Decided by Jens 2026-09-29:** nothing more on the page; the status already
  reads "Inactive: upscaling was switched off." Closed.
- **42.** **doc/decide, S** – "Wayland Auto makes no request" disclaimer versus the
    fractional scale now being requested (L84-90, L717, L1269).
  **Decided by Jens 2026-09-29 and done:** the documents describe what the code
  does; the false statement is corrected in the profiles slice, and the
  supported scope stays with 19a.
- **43.** **decide** – 60 strings from the 2026-09-21 text review (L1350).
- **44.** **decide → impl** – Text review batch 3 and the texts after it (L1360-1368).
  **Decided by Jens 2026-09-29 for both:** the table of 2026-09-21 was never
  saved and the texts have changed since, so every user-facing string is
  extracted afresh from the source, grouped by where it appears, with a
  proposed wording beside each, on a private review page where Jens accepts,
  rejects or rewrites each one; his answers are applied and tested.
  **Done 2026-09-29:** 310 strings extracted, 37 wordings proposed with a
  reason each, the catalogue note among them; Jens accepted all 37 and left
  the rest as they are. Applied, with the tests and the handbook passages that
  quote them. The X11 request's failure reasons are clauses now, lowercase and
  without a full stop, since they follow "request failed:"; sizes use "×"
  throughout; "Restart required" is "Log out required"; American spelling.
- **45.** **decide** – Settings page layout, awaiting review in System Settings
    (L1284).
  **Approved by Jens 2026-09-29.** Closed.
- **46.** **decide** – Migration for a stored `ResolutionControl=false` (L1337).
  **Decided by Jens 2026-09-29:** no migration; the key never shipped in a
  release. Closed.
- **47.** **decide** – Flatpak app ID as its own field (L1136).
  **Decided by Jens 2026-09-29:** no field of its own; a Flatpak program is named
  URL-style, as a Wine program is, with its app ID in the authority, for
  instance `flatpak://net.supertuxkart.SuperTuxKart/app/bin/supertuxkart`, and
  matched by the same pattern fields. The proxy's identity header reserved
  such schemes for container runtimes. Checked against a real Flatpak game
  with 51.
  **Implemented 2026-09-29:** the proxy reads the application's ID from
  `/proc/<pid>/root/.flatpak-info` and names the program
  `flatpak://<id>/app/...` before its path in the sandbox; the effect names a
  Wayland program the same way from the security context's app ID KWin keeps,
  for a path below `/app`, at bind and for its window. Unit cases in the
  proxy's identity test and the matching test, with the description and path
  51 observed; not yet seen in a session with a build that has it.
- **48.** **impl/test** – Portable "Add from Window" (path below the library root as
    a pattern), export/import across users (L1352-1359).
  **Done 2026-09-29, `aa7053b`, pushed:** a Steam game is stored by its folder in the
  library and its path there, anything else by its file name in any folder, as
  a regular expression; an exported entry matches another user's copy. Not
  checked with two real accounts. Recorded in the slice and the handbook.
- **49.** **impl** – Translate the catalogue notes (L514-516).
- **50.** **test, F** – Real-session validation of the recommended values; TV
    (L1253, L1261, L1386).
- **51.** **decide → investigate, F** – Flatpak/Snap identities for STK/ETR; needs a
    Flathub remote and snapd on the host (L1744-1831).
  **Flatpak observed 2026-09-29** in the Fedora 43 package machine, not on the
  host, with Flathub's SuperTuxKart 1.5 and nightly 36545995686's effect: the
  system resolves its process to `/app/bin/supertuxkart`, the path inside the
  sandbox; Flatpak 1.16.6 connects it to KWin through a security context
  (sockets in `$XDG_RUNTIME_DIR/.flatpak/wl`) and describes the sandbox in
  `/.flatpak-info` (`[Application] name=net.supertuxkart.SuperTuxKart`); its
  Wayland window has class `supertuxkart` and no instance. The shipped entry
  claimed it, asked it for 2560 × 1440 and got that buffer; it was not
  enlarged only because the machine's screen had locked while idle. Left: the
  same with a build that has 47, the X11 route through the proxy, and Snap.
  Its remainder is listed as K9 in the [known limitations](slice-known-limitations.md).
- **52.** **decide** – Submitted applications: ship unverified submissions? credit?
    scale? (L1681)
  **Decided by Jens 2026-09-29:** accepted submissions ship active, with their
  provenance in the entry; credit through Git history and release notes, no
  names in the installed file; and the list is indexed by program and window
  identity at load time now, rather than measured later. The index goes with 53.
- **53.** **impl/test, S** – Submitted applications route: program name in the
    report, copy action, `CONTRIBUTING.md`, issue form, catalogue tests,
    end-to-end rehearsal (L1681-1688). Not started.
  **Implemented 2026-09-29 except the rehearsal:** the load-time index, the
  effect's `reportFacts`, **Copy Report…** and its report, the acceptance rule
  as a catalogue test, `CONTRIBUTING.md` and the application form. Left: the
  end-to-end rehearsal in a real session, and the form checked on GitHub (88).

## E. What the effect says – `slice-development-infrastructure.md`

- **54.** **investigate** – Minimum KDE version with the About dialog APIs (L368).
  **Done 2026-09-28:** `KAboutPluginDialog` since KF 5.65, present in Trixie's
  6.13; notices need `KAboutApplicationDialog`. Both need KXmlGui as a new
  build dependency of the settings module. Recorded in the slice.
- **55.** **impl** – Notices audit; AMD shader entry in `debian/copyright` (L370).
  **Done 2026-09-28, `0ac6bd1`, pushed.** Recorded in the slice.
- **56.** **impl** – About access in settings: dialog, full hash, tag, offline
    notices (L371-375).
  **Done 2026-09-28, `0ac6bd1`, pushed, except the settings page:** branch,
  tag, full commit and the date's origin are fields, a source archive records
  its provenance, the log gets the whole record, and the notices are installed
  offline. Left for 56a.
- **56a.** **decide** – Whether the settings page gets an About dialog with the
  notices: `KAboutPluginDialog` and `KAboutApplicationDialog` need KXmlGui as a
  new build dependency of the settings module (item 54). Found 2026-09-28.
  **Decided by Jens 2026-09-29:** no dialog; KWin's own About in the Desktop
  Effects list suffices, and the notices stay an installed file. Closed.
- **57.** **impl, S** – `website` metadata field (L927).
  **Closed 2026-09-28, not added:** no KWin effect declares a website, and a
  link to this repository would be project-specific in the plugin folder.
  **Reopened and done the same day, `0ac6bd1`, pushed:** that closure
  contradicted Jens's decision of 2026-09-21 for the About page; `Website` and
  `GPL-2.0-or-later` are in the metadata.
- **58.** **test, S** – Incremental build-identity checks (L298-308).
  **Done 2026-09-28, `51e56ff`, pushed, except in a session:** measured with Ninja; only
  `buildinfo.cpp` recompiles, and a new commit reaches the binaries without
  reconfiguring. The installed pair after an upgrade needs a session.
- **59.** **test** – Package and source archive with and without the KCM (L352-355).
  **Done 2026-09-28, `044a366`, pushed.** Recorded in the slice.
- **60.** **test, S** – Transition-logging acceptance: no flooding, separate debug
    tracing, no environment dumps (L338-341, L383).
  **Done 2026-09-28, `e31d3a7`, pushed:** implemented since #21; `logsTransitionsNotFrames`
  requires sixty unchanged frames to log nothing. Recorded in the slice.
- **61.** **impl** – Shortcut to toggle the displays (L385).
  Needs Jens first: which displays one key toggles, and its default key.
  **Jens 2026-09-29:** a later feature, not part of this release. Deferred.
- **62.** **impl, S** – Heads-up wording: "native" only when sizes are equal, named
    resolutions only at exact sizes (L436-444).
  **Closed 2026-09-28:** done since #17 (`35b6a53`) and covered by two
  heads-up tests; the slice's note was stale.
- **63.** **impl** – Lay the text out again when fonts change (L430-432).
  **Done 2026-09-28, `439d625`, pushed;** what the platform theme reports after
  a change in a real session goes with the hardware checks.
- **64.** **test, S/F** – `kill -9` a game repeatedly, watch memory (L418-424).
  **Done 2026-09-29, native half:** in the Arch package machine (KWin
  6.7.5, llvmpipe, the nightly's package), SuperTuxKart was started twelve
  times, enlarged each time, and killed with `kill -9`. KWin's resident
  memory after each kill settled at 410,228 kB from the fifth round and was
  410,240 kB after the twelfth; open files and threads came back to 163 and
  18 every time. Under llvmpipe textures are KWin's own memory, so this
  covers the video memory the effect allocates. The nested-session half is
  the crash cases' records check. Recorded in the infrastructure slice.
- **65.** **test, S** – Per-game display settings against the opposite global
    (L1144-1146).
  **Closed 2026-09-28, `51e56ff`, pushed:** covered since #21 by
  `perGameDisplaySettings`. Recorded in the slice.
- **66.** **test, S** – 2026-09-27 display change: seven suites, Clang, neon, tidy
    and both pre-commit stages never finished; the machine went down during
    `upscale-x11-integration` (L522-530).
  **Closed 2026-09-28:** every check named ran in the full run on `f68eab0`'s
  tree. Recorded in the slice.
- **67.** **doc** – Reload tool: temporary-name limitation, fresh session (L1203-1205).
  **Closed 2026-09-28:** both limitations are in the README's "Reload during
  development". Recorded in the slice.
- **68.** **impl, S** – Translations: `Messages.sh`, `po/`, `ki18n_install`, `i18nc`,
    de/fr/es, metadata, incomplete-catalogue check (L1099-1130).
- **69.** **doc/decide** – Interactive controls: not started; only a hardware gate,
    so it can never close as written (L942-1021).
  **Decided by Jens 2026-09-29:** a later feature. It stays specified in the
  handbook as planned and leaves this release's gates; automated checks come
  beside the hardware gate when it is taken up. Deferred.
- **70.** **test, F** – Displays on the TV (legibility, SDR/HDR, VRR, lock), placement
    at scale 3, footer and metadata by eye (L342-346, L691-694, L927-930).

## F. FSR rendering – `slice-fsr1-hdr-vrr.md`

- **71.** **test, S** – A scaled frame on hardware: pixel comparison, `activeEffects`,
    fallback (L60-70, L316).
- **72.** **test, S** – Lifecycle and fallback integration acceptance (L318).
  **Done 2026-09-28, `51e56ff`, pushed:** the VM production test passed all eight cases on
  KWin 6.3.6 with the OpenGL virtual backend. Recorded in the slice.
- **73.** **test, S** – A0/A1 with phase-reversed repeats (L28-31, L570).
- **74.** **test, F** – B–D cost matrix, real games, image quality, HDR (L320-323).
- **75.** **decide, F** – VRR: HDMI-A-1 on wzpc reports adaptive sync incapable, the
    NVIDIA host has VRR disabled; which host/link (L38-41, L541-546).
  **Decided by Jens 2026-09-29:** HDR and VRR are postponed to a later version.
  When taken up, VRR is accepted on the NVIDIA host (pcjensd) and on wzpc.
  Deferred, with the HDR and VRR parts of 70 and 74.
- **76.** **impl** – Aspect ratio and integer scaling: specified, not implemented
    (L750-757).
  **Decided by Jens 2026-09-29:** built for 0.3.0, Fit and Integer both.
  **Built 2026-09-29:** picture geometry in device pixels, black bars, the
  nearest path without sharpening, the Picture size and Scaling filter
  settings with entry overrides by name, the status, and pointer mapping onto
  the picture for Wayland, for X11 in a mode of its own (KWin 6.6 on) and for
  X11 the effect presents. Unit, render and session tests on GL and GLES, each
  input case seen failing against its defect. Left: relative pointer, lock,
  popups and overlays with bars, HDR/VRR, and the TV acceptance with real
  retro and differing-aspect games. Recorded in the rendering slice.
- **77.** **test** – PR #14 GLES combined-candidate validation still "pending" (L640).
  **Closed 2026-09-28, `51e56ff`, pushed:** `allocationIgnoresEarlierErrors` passes in
  both render suites in every full check run. Recorded in the slice.

## G. Wayland conformance – `slice-wayland-conformance.md`

- **78.** **test, S** – Second pre-commit stage, Clang and neon after the output
    lifecycle fix; not recorded as run.
  **Closed 2026-09-28:** the second stage, Clang and neon ran in the full
  run on `f68eab0`'s tree; the full comparison stays with the release gate.

## H. Pipeline modules – `slice-pipeline-modules.md`

- **79.** **test, S** – A green nightly that publishes every target; record stage cost.
- **80.** **impl** – Test the FreeBSD package in an emptied machine or a jail.
  **Done 2026-09-29, `5c117bd`, pushed:** the machine is emptied between build
  and test; the verify-only nightly 36501169792 passed it. Recorded in the
  pipeline slice.
- **80a.** **watch** – The same nightly's Debian arm64 builds differed, where amd64
  and three earlier arm64 runs built identically. The comparison now lists the
  differing files. The next nightly, 36503302445 on nearly the same sources,
  built arm64 identically, so the difference is intermittent; the listing names
  the files when it comes back. Found 2026-09-29.
- **81.** **decide** – Nightly checks CI's conclusion through the API instead of
    rerunning `ci.yml`.
  **Decided by Jens 2026-09-29:** check through the API; CI runs itself only for a
  commit with no completed CI run. **Implemented the same day:**
  `tools/ci-conclusion.py` and the nightly's `ci` job; the instrumented tests
  keep the nightly's 600-second fuzzing. Awaits its first nightly.
  **First nightly 2026-09-29, verify-only 36545995686 on `7ff0fe6`:** the `ci`
  job read the pull request's green run as passed, CI was skipped, and the
  instrumented builds ran with the nightly's fuzzing, all three green.
- **82.** **investigate/fix** – An openSUSE amd64 package failure; not observed in
  ten nightlies. **Jens 2026-09-29:** found by the agent, not reported by him;
  the agent finds it and solves it.
  **Found and fixed 2026-09-29:** the first attempt of nightly 35519298024
  (2026-09-20, master) got 403 from `download.opensuse.org` while refreshing
  the repositories; its rerun passed. Every openSUSE download is now tried up
  to four times. Recorded in the pipeline slice; the nightly confirms it.
  The nightly 36545995686 built and tested both openSUSE packages; its only
  failure was the same kind of thing elsewhere: neon's archive mid-sync ("File
  has unexpected size ... Mirror sync in progress?") failed the neon Clang
  image. The neon image now retries its downloads the same way.

## I. Build and release pipeline – `slice-build-release-pipeline.md`

- **83.** **test** – First rolling-nightly publication and a stable-tag release, with
    downloaded-asset and provenance verification.
- **84.** **test** – Outside-author pull request; live CodeRabbit approval revocation.
- **85.** **doc** – Dependabot scheduled run is now observed (#22 today); claimed by
    both this slice and the GitHub one; pick one owner.
  **Done 2026-09-28:** the GitHub workflow slice owns the observation (#22,
  merged 2026-09-28); the pipeline slice owns the configuration.

## J. GitHub project workflow – `slice-github-project-workflow.md`

- **86.** **impl** – Section 4: `.github/release.yml`, workflow summaries, SBOM, Pages
    (none exist).
  **Done 2026-09-28 except Pages, `ffad4a7`, pushed:** categorized release notes, a
  summary for every check job, and an SPDX SBOM in every release, validated
  and attested. Pages is 86a. Recorded in the slice.
- **86a.** **decide** – GitHub Pages for the handbook: turning it on is a
  repository setting, and what the site holds and excludes follows from it.
  **Decided by Jens 2026-09-29:** yes; a workflow publishes the handbook and the
  permanent documents, never doc/agents/. **Implemented the same day:**
  `tools/build-site.py` (the README as front page and the documents under doc/,
  links to anything else pointed at GitHub, unit-tested) and `pages.yml`, run
  on every push to master; Pages is switched on for workflow builds at
  <https://jensksp.github.io/kwin-effect-upscale/>. Built locally with the Pages
  action's own image: every page and all 27 of the handbook's tables. Its first
  hosted run follows the merge.
- **87.** **decide** – Release milestones (none exist).
  **Decided by Jens 2026-09-29:** a 0.3.0 milestone now, without a due date,
  holding the issues and pull requests that block it, and one per release from
  then on. **Done the same day:** milestone 1, "0.3.0", holds #23, the only
  open issue or pull request.
- **88.** **test** – Issue forms render and reject empty required fields on GitHub.
- **89.** **impl** – Hook-update pull requests; failure notifications.
  **Partly done 2026-09-28, `49cf326`, pushed:** `tools/update-hooks.py` proposes updates
  every Monday in the run's summary, holding clang-format to 19 and never moving
  a pin back. Left for 89a.
- **89a.** **decide** – How the hook update opens its pull request: a PR made
  with the run's own token starts no workflow, so it needs a GitHub App or a
  fine-grained token as a secret, or a CI dispatch on its branch. And Jens's
  notification settings for failed scheduled runs and security alerts, which
  the CLI's token cannot read without the `notifications` scope.
  **Decided by Jens 2026-09-29:** the workflow pushes its branch, opens the pull
  request with its own token and dispatches CI on that branch itself; no new
  secret or app. For notifications, Jens grants the CLI the `notifications`
  scope (`gh auth refresh -s notifications`) and the agent reads and reports
  the settings. Both now implementation items. **The pull request half is
  implemented the same day** in `hook-updates.yml`: a dated branch
  `hook-updates-<day>` from the branch the run started on, a pull request with
  the run's token, and CI dispatched on the branch; none is opened while an
  earlier one is open, so nothing is pushed over. Not run yet: its first run is
  the scheduled one on master. The notification half waits for Jens's
  `gh auth refresh -s notifications`.
- **90.** **doc/investigate** – CodeQL ran once (2026-09-21, success); the document
    says never; findings not assessed.
  **Done 2026-09-28, `51e56ff`, pushed:** analyses on 2026-09-21 and 2026-09-28 for all
  three languages, no alert. Recorded in the slice.
- **91.** **decide** – Non-provider secret patterns and validity checks.
  **Decided by Jens 2026-09-29:** both on. Tried the same day through the API
  (`PATCH /repos/…` with `security_and_analysis`): answered 200, both stayed
  disabled. Presumably not offered for a public repository of a personal
  account; Jens checks Settings → Code security, where they would be switched
  on if offered.

## K. The documents themselves

- **92.** **doc** – Stale status sections in resolution control, application
    profiles, Proton, what the effect says and FSR; several still call #21
    pending.
  **Done 2026-09-28, `51e56ff`, pushed:** a dated status paragraph heads each of the five.
- **93.** **doc** – Application profiles: "four entries" (six ship), "six slots"
    (four since 2026-09-25).
  **Done 2026-09-28:** six shipped entries and four slots, corrected.
- **94.** **doc** – Contradictions to settle: global slot defaults and method
    inheritance (profiles); physical input in S or F (resolution control);
    sampling while hidden (what the effect says).
  **Two of three settled 2026-09-29:** slot defaults and inheritance
  follow Jens's decisions of 2026-09-21 and the code (a game's absent slot
  inherits, the global one is Auto); sampling while hidden follows the code
  and a run (presentation sampling continues, the display's own stops). Left
  for 94a.
- **94a.** **decide** – Physical input in the supported scope or in full
  acceptance: the resolution slice's supported scope asks for physical-display
  acceptance of the supported client class, while its later gate and the full
  acceptance list put physical input under full acceptance, and item 29 is
  marked supported scope.
  **Decided by Jens 2026-09-29:** supported scope. A release needs input checked
  on a real display for the supported games; the slice's later gate and full
  acceptance list are corrected to match.
