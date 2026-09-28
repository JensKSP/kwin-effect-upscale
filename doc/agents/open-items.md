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
  prepared-session tests, which wait for item 32.
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
  3) and passes with it (3 of 3). Commit and hosted run pending.
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

  **Fixed 2026-09-28, pending commit:** all five, the proxy with two unit
  cases in `x11proxy_display_test.cpp`, the tools with
  `tools/test_check_presentations.py` and a case in
  `test_measure_frame_times.py`; each new tool test failed on the old code.

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
  **Fixed 2026-09-28, pending commit:** `fullscreenRequest()` began a request
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
  `session.cpp`. Passes with GCC and under both sanitizers.
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
- **15.** **fix, –** – Incoherent advertisement: `wl_output.mode` falsified beside a
    truthful `xdg_output`; recorded as a defect in shipped code, no fix
    recorded (L1363-1365, L1841-1851).
  **Agreed with Jens 2026-09-28:** the fix is to make `xdg_output` consistent
  with the told mode (logical size = mode ÷ scale), sent when the program asks
  for its `xdg_output` and given back on restore. To find out first whether the
  effect can reach one program's `xdg_output` on KWin 6.3.6 and 6.6. If stuck,
  call in Fable.
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
- **17.** **investigate, –** – SuperTuxKart writes the reduced mode into its own
    configuration; to be answered from the compositor side (L1256-1264).
- **18.** **impl, –** – A program that sizes a plain window from the mode it was told
    has to be presented over its screen (L1345-1347).
- **19.** **test, –** – The seven-item Auto bench was never run (L1853-1886).
- **20.** **investigate, F** – Six source-led investigations unticked: glmark2 X11,
    integer-scale reachability, ETR secondary output, SuperTux borderless and
    render cost, Wine/Proton D3D presentation (L1546-1634).
- **21.** **decide, –** – `UserConfigured` method (L2253) versus the later "On/Off
    per presentation" plan (L3586-3588): which one stands.
- **22.** **test, F** – Physical acceptance: mixed resolutions and scales, output
    movement, input and confinement, TV (L2116-2125).
- **23.** **impl/test, F** – Proxy: Vulkan and presentation sync, overhead, container
    identity (Flatpak, Snap, Docker), BSD (L3308-3310, L3360-3363, L3487-3489).
- **24.** **test, –** – SuperTuxKart on X11 through the proxy never recorded; source
    of Jens's Auto report; ~1 s stalls on 6.6 (L1918-1920, L1950, L3687-3690).

## C. Proton smaller screen – `slice-proton-smaller-screen.md`

- **25.** **doc + decide** – Gates and acceptance criteria still describe the retired
    prefix companion; the proxy route has no gate (L326-352).
- **26.** **impl, S** – Hold a prefix's first connection until its first real program
    appears, instead of answering within 500 ms (L791-796).
- **27.** **decide/investigate** – Warm prefix: a running wineserver opens no new
    connection; no answer proposed (L797-798).
- **28.** **impl/test, S** – The 2026-09-25 decisions: prefix as the unit, path-tail
    matching, launchers, windowed left alone, per-prefix candidate report
    (L764-787).
- **29.** **test, S** – Real Wine input at scale 3 (L748-752, L829-835).
- **30.** **test, S** – Wreckfest on wzpc through the proxy (L468-472, L925-927).
- **31.** **test, S** – One Wine game outside Steam (L328-330).
- **32.** **decide/impl** – Legacy Wine guards and the Helper1 path: remove or keep
    (L663-671, L744).
- **33.** **wzpc** – Restore global `OsdStatistics=true` from
    `build/wzpc-clean-start-94a6804/` (L632-636).
- **34.** **test/investigate, F** – Steam as Flatpak and Snap; the 09-27 analysis
    still reasons with the companion (L870-927).
- **35.** **impl, F** – Present Wine's Wayland driver (L313-319).
- **36.** **test, F** – Every flavour × D3D9/11/12/OpenGL/Vulkan × exclusive/borderless
    (L169-174, L335-339).
- **37.** **doc** – Close the moot helper translation-domain question; owe the
    handbook update in proxy form (L575-579, L592).

## D. Application profiles – `slice-application-profiles.md`

- **38.** **test, S** – Trixie Clang and neon GCC/Clang on the tree after the windowed
    slots were removed; only Trixie GCC recorded (L1737-1739).
- **39.** **test, S** – Migration from a real configuration written by the previous
    release (L81-82, L915-934).
- **40.** **impl, S** – "Clear a profile's overrides" in the editor: required, not in
    the plugin (L60-62, L1244-1246).
- **41.** **decide, S** – What the page shows when a stored `Enabled=false` stops
    every profile (L1380).
- **42.** **doc/decide, S** – "Wayland Auto makes no request" disclaimer versus the
    fractional scale now being requested (L84-90, L717, L1269).
- **43.** **decide** – 60 strings from the 2026-09-21 text review (L1350).
- **44.** **decide → impl** – Text review batch 3 and the texts after it (L1360-1368).
- **45.** **decide** – Settings page layout, awaiting review in System Settings
    (L1284).
- **46.** **decide** – Migration for a stored `ResolutionControl=false` (L1337).
- **47.** **decide** – Flatpak app ID as its own field (L1136).
- **48.** **impl/test** – Portable "Add from Window" (path below the library root as
    a pattern), export/import across users (L1352-1359).
- **49.** **impl** – Translate the catalogue notes (L514-516).
- **50.** **test, F** – Real-session validation of the recommended values; TV
    (L1253, L1261, L1386).
- **51.** **decide → investigate, F** – Flatpak/Snap identities for STK/ETR; needs a
    Flathub remote and snapd on the host (L1744-1831).
- **52.** **decide** – Submitted applications: ship unverified submissions? credit?
    scale? (L1681)
- **53.** **impl/test, S** – Submitted applications route: program name in the
    report, copy action, `CONTRIBUTING.md`, issue form, catalogue tests,
    end-to-end rehearsal (L1681-1688). Not started.

## E. What the effect says – `slice-development-infrastructure.md`

- **54.** **investigate** – Minimum KDE version with the About dialog APIs (L368).
- **55.** **impl** – Notices audit; AMD shader entry in `debian/copyright` (L370).
- **56.** **impl** – About access in settings: dialog, full hash, tag, offline
    notices (L371-375).
- **57.** **impl, S** – `website` metadata field (L927).
- **58.** **test, S** – Incremental build-identity checks (L298-308).
- **59.** **test** – Package and source archive with and without the KCM (L352-355).
- **60.** **test, S** – Transition-logging acceptance: no flooding, separate debug
    tracing, no environment dumps (L338-341, L383).
- **61.** **impl** – Shortcut to toggle the displays (L385).
- **62.** **impl, S** – Heads-up wording: "native" only when sizes are equal, named
    resolutions only at exact sizes (L436-444).
- **63.** **impl** – Lay the text out again when fonts change (L430-432).
- **64.** **test, S/F** – `kill -9` a game repeatedly, watch memory (L418-424).
- **65.** **test, S** – Per-game display settings against the opposite global
    (L1144-1146).
- **66.** **test, S** – 2026-09-27 display change: seven suites, Clang, neon, tidy
    and both pre-commit stages never finished; the machine went down during
    `upscale-x11-integration` (L522-530).
- **67.** **doc** – Reload tool: temporary-name limitation, fresh session (L1203-1205).
- **68.** **impl, S** – Translations: `Messages.sh`, `po/`, `ki18n_install`, `i18nc`,
    de/fr/es, metadata, incomplete-catalogue check (L1099-1130).
- **69.** **doc/decide** – Interactive controls: not started; only a hardware gate,
    so it can never close as written (L942-1021).
- **70.** **test, F** – Displays on the TV (legibility, SDR/HDR, VRR, lock), placement
    at scale 3, footer and metadata by eye (L342-346, L691-694, L927-930).

## F. FSR rendering – `slice-fsr1-hdr-vrr.md`

- **71.** **test, S** – A scaled frame on hardware: pixel comparison, `activeEffects`,
    fallback (L60-70, L316).
- **72.** **test, S** – Lifecycle and fallback integration acceptance (L318).
- **73.** **test, S** – A0/A1 with phase-reversed repeats (L28-31, L570).
- **74.** **test, F** – B–D cost matrix, real games, image quality, HDR (L320-323).
- **75.** **decide, F** – VRR: HDMI-A-1 on wzpc reports adaptive sync incapable, the
    NVIDIA host has VRR disabled; which host/link (L38-41, L541-546).
- **76.** **impl** – Aspect ratio and integer scaling: specified, not implemented
    (L750-757).
- **77.** **test** – PR #14 GLES combined-candidate validation still "pending" (L640).

## G. Wayland conformance – `slice-wayland-conformance.md`

- **78.** **test, S** – Second pre-commit stage, Clang and neon after the output
    lifecycle fix; not recorded as run.

## H. Pipeline modules – `slice-pipeline-modules.md`

- **79.** **test, S** – A green nightly that publishes every target; record stage cost.
- **80.** **impl** – Test the FreeBSD package in an emptied machine or a jail.
- **81.** **decide** – Nightly checks CI's conclusion through the API instead of
    rerunning `ci.yml`.
- **82.** **decide/investigate** – The openSUSE amd64 failure you reported; not observed.

## I. Build and release pipeline – `slice-build-release-pipeline.md`

- **83.** **test** – First rolling-nightly publication and a stable-tag release, with
    downloaded-asset and provenance verification.
- **84.** **test** – Outside-author pull request; live CodeRabbit approval revocation.
- **85.** **doc** – Dependabot scheduled run is now observed (#22 today); claimed by
    both this slice and the GitHub one; pick one owner.

## J. GitHub project workflow – `slice-github-project-workflow.md`

- **86.** **impl** – Section 4: `.github/release.yml`, workflow summaries, SBOM, Pages
    (none exist).
- **87.** **decide** – Release milestones (none exist).
- **88.** **test** – Issue forms render and reject empty required fields on GitHub.
- **89.** **impl** – Hook-update pull requests; failure notifications.
- **90.** **doc/investigate** – CodeQL ran once (2026-09-21, success); the document
    says never; findings not assessed.
- **91.** **decide** – Non-provider secret patterns and validity checks.

## K. The documents themselves

- **92.** **doc** – Stale status sections in resolution control, application
    profiles, Proton, what the effect says and FSR; several still call #21
    pending.
- **93.** **doc** – Application profiles: "four entries" (six ship), "six slots"
    (four since 2026-09-25).
- **94.** **doc** – Contradictions to settle: global slot defaults and method
    inheritance (profiles); physical input in S or F (resolution control);
    sampling while hidden (what the effect says).
