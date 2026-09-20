<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: what the effect says, and in which language

## Status and remaining work

The shared snapshot, refusal-specific diagnostics and passive OSD are implemented.
They identified the render-target orientation defect; the rendering slice records
its fix and a nested real-GPU SuperTuxKart observation. The early diagnostics-first
sequence is therefore historical, not the next unimplemented capability.

This package remains open for the full About/build identity and notices inventory,
transition logging, session-font handling and its remaining native acceptance.
It also carries two topics merged into it on 2026-09-20, neither of them started:
the interactive half of the same on-screen surface, and shipping every text in
the user's language. Both are recorded under
[absorbed topics](#absorbed-topics) with their own gates and open items.
Current presentation statistics remain observable even with the OSD hidden;
overlay drawing and its own client/repaint sampling stop when hidden.
Rendering, resolution-control and pipeline acceptance retain their own owners.

## Start state

The plugin has no About action in its settings. Its generated build source
provides version, branch, UTC date/time and a log description containing Qt's
version. A static initializer prints the description when the effect library
loads. There is no separate public hash/tag field; the abbreviated hash occurs
in snapshot versions but disappears from a clean release version.

The effect metadata declares the name and a generic GPL license, without
authors, website or version. AMD shader headers retain their copyright and MIT
text, and the repository carries license texts, but there is no installed
component-notice viewer or audited inventory covering the shipped build.
This section describes the state before implementation began; what has been
implemented since is recorded under progress and remaining work below.

The settings module can show supplied-buffer status, and the effect logs a
render-resource failure. Detection OSD and persistent statistics are specified
but not implemented. There is no shared developer state snapshot, build-type
OSD defaults or comprehensive transition logging. Profiles, controlled launches
and live client-resolution negotiation are separate future work.

## End state

Complete when a developer can identify the exact loaded build, inspect its
effective settings and observed state in the passive overlay or settings,
follow meaningful transitions in logs, and read the specific reason the effect
refused or accepted any given window. Settings provide the complete KDE-style
About and component/license view; initialization logs the same identity once,
and installed notices remain accessible offline without the KCM. Debug builds
default to persistent statistics plus developer information; release builds
default to timed detection/basic-setting messages. Explicit preferences survive
build changes. Component attribution and release contents agree, and all
automated, incremental-build, package and native acceptance below has passed.

## Scope and boundaries

Own the shared About/build record, metadata population, settings entry and
linked details, initialization and transition logging, runtime snapshot,
notices inventory and installed license resources. Own the shared passive
overlay surface, detection/basic-setting messages, statistics, developer view,
visibility defaults and resource lifecycle. The
[handbook](../upscaling.md#about-build-identity-and-third-party-notices) provides
the permanent specification for identity and notices;
[logging](../upscaling.md#diagnostic-logging-and-state),
[OSD defaults](../upscaling.md#osd-defaults-by-build-type) and
[developer information](../upscaling.md#developer-information) define diagnostics.

The optional full in-game About dialog does not block this package. Interactive
game-setting controls, comparison and the language the texts ship in are owned
here as well, under [absorbed topics](#absorbed-topics); they have their own
end states and do not gate the diagnostics work above. Game-profile implementation,
process management, resolution negotiation, new scalers/geometry, rendering
algorithm changes and release-workflow redesign are excluded. Observe existing
behaviour rather than extending it to populate future diagnostic fields. Do
not turn this task into a general system-wide software inventory.

## Dependencies

Reuse the existing version generator and KWin settings module. Coordinate the
source-archive metadata and installed notice resources with the
[release pipeline](slice-build-release-pipeline.md), without duplicating its
publication work. Keep generated build metadata outside the plugin folder and
retain its unchanged-copy requirement for upstream KWin. Read current renderer
selection, buffer, settings and resource state through a shared diagnostic
interface. This slice does not depend on future profiles or launch helpers:
label the current selected fullscreen client accurately, without claiming a
profile match or verified game identity. Show unavailable capabilities honestly.
The [profiles](slice-application-profiles.md) package later supplies recognized
game/profile events and per-profile overrides to the existing OSD; the
interactive controls below extend the same surface. Test those interfaces with controlled
states now; integrated profile/launch acceptance stays with their owning slices.

## Approach

1. Verify the installed minimum/current KDE About APIs and settings host. Use
   the standard host action when suitable, otherwise a small About button in
   the KCM opening a KDE dialog with linked build/component details.
2. Define one immutable identity record. Run its generator on every build,
   including source-unchanged and direct effect/KCM target builds; recompute the
   full revision, ref/tag and timestamp with `SOURCE_DATE_EPOCH` support. Use
   content comparison to limit compilation to a small generated source and
   linking of affected modules. Preserve source identity in archives, keep
   missing data explicit, and separate installed versus loaded binaries.
3. Populate standard plugin metadata for name, authors, website, license and
   base version without introducing another hand-maintained version number.
   Volatile build values must not modify embedded JSON, resources or headers;
   add them to the About data at runtime. Resolve how the generated provider
   and KCM access work with the upstreamable folder
   boundary before adding cross-directory includes or target dependencies.
4. Audit incorporated files, linked libraries and relevant transitive/bundled
   components. Preserve exact authorship, copyright, license expressions,
   exceptions and required notices. Include full offline texts and align Debian
   copyright/install metadata with the actual artifacts.
5. Implement settings access, copying and explicit link activation, plus a
   bounded initialization log using the same record. Opening About must neither
   apply settings nor load the effect solely to inspect its identity.
6. Validate identity consistency and missing-data cases, release/source/archive
   notices, standard KDE interaction and independent packaging without the KCM.
7. Define one coherent diagnostic snapshot of current effective settings,
   selected window/output, processing state and labelled measurements. Expose
   inspection/copying from settings and use the same data for the developer
   view and bounded debug-level state-transition logs. Keep routine warnings
   actionable and initialization information visible by default.
8. Implement the shared output-resolution OSD outside game capture, with
   independent timed announcements and persistent statistics/developer fields.
   Apply the handbook's Debug/release defaults only to absent preferences.
   Use event-driven bounded sampling; hiding the persistent view stops its
   sampling work, and hiding all modes releases their rendering resources.
9. Validate the existing effect with these diagnostics in both build types,
   including capture exclusion, input, output changes, locking and cleanup.

## Detection and visibility contract

Use one surface for timed detection/basic-setting summaries and persistent
statistics/developer information. A suggested initial timeout is three seconds,
positive and bounded, with disabling notifications separate from duration.
The basic summary names the observed scale and shader path or bypass reason.
Future profile overrides inherit global values unless explicitly set, including
an explicit Off. A disabled processing path can still have a recognized game.

Announce once per active, visible window and selected identity/profile, including
late identity. Keep notification history separate from cached settings so
repaint, title changes, focus return and unchanged reconfiguration do not repeat
messages. A new window or changed winning profile may announce again. A real
effective-setting change may show a fresh timed basic summary. Replace obsolete
messages instead of queuing them. Before profile integration, use a neutral
selected-application message; do not invent a recognized-game event.

Hide timed content on expiry, window destruction, loss of active/visible state
or notification disable. Suppress all OSD modes while locked and discard stale
game/output state. Do not count a suppressed message as displayed. A detection
timeout does not hide an enabled persistent view. Passive content takes no
focus or input, never enters the captured game texture, and cannot change
candidate selection. Release overlay resources and its composition requirement
when hidden; do not present overlay visibility as evidence of active VRR.

## Source findings, 2026-09-18

These are source/documentation observations, not a runtime or legal-completeness
result for the proposed feature.

- Inspected KWin v6.3.6 at `b8de4329447824b1b1e7a36b3a57acfd069f1423`
  and cached master at `7db8e19725edad20135d8a63a19ae52e0a3ef4b9`.
  Both effects-list delegates show author/license text for the selected item and
  a configure button; `EffectsModel::requestConfigure` creates `KCMultiDialog`.
  Neither inspected delegate establishes a complete per-effect About action
  inside our settings. The blur KCM is a plain `KCModule`, like ours.
  [KWin effects delegate](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/kcms/effects/ui/Effect.qml),
  [configuration host](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/kcms/common/effectsmodel.cpp).
- KDE supplies [KAboutPluginDialog](https://api.kde.org/kaboutplugindialog.html)
  driven by [KPluginMetaData](https://api.kde.org/kpluginmetadata.html).
  [KAboutData](https://api.kde.org/kaboutdata.html) also supports component
  entries and license data. These APIs are candidates for standard presentation;
  verify the exact minimum-version UI and whether linked custom details are
  needed for complete copyright/notice texts. Do not change the host process's
  global About data to represent an effect.
- `src/buildinfo/buildinfo.cpp.in` logs through a static initializer, and
  `cmake/KWinBuiltinEffectShim.cmake` injects that source into the effect only.
  Simply linking it into the KCM would also execute the logger in the settings
  process. Separate access to data from effect-initialization logging.
- `GenerateBuildInfo.cmake` currently computes a ten-character Git hash and
  combines branch/tag-like CI refs in one branch field. The template exposes
  neither an independent full hash nor a tag. `BuildInfoDate.cmake` already
  honours `SOURCE_DATE_EPOCH` but retains the previous timestamp for unchanged
  inputs. That timestamp caching does not meet the newly required regeneration
  on every invocation and must be removed while keeping content-aware writes.
- `tools/build-source.py` preserves `source-version` in generated archives;
  it does not currently retain a complete original commit/ref record. Extend
  archive provenance without claiming a branch for arbitrary Git-free sources.
- `easu.glsl` and `rcas.glsl` identify AMD FSR 1 `v1.20210629`, copyright 2021
  Advanced Micro Devices, Inc., under MIT, and document local adaptations.
  Preserve their exact notices. `debian/copyright` currently has an MIT entry
  for `.clang-format` but no AMD shader-specific entry, so its catch-all GPL
  declaration needs correction before claiming the notices inventory complete.
- Qt modules, KDE Frameworks, KWin and graphics dependencies are visible in the
  CMake targets and `debian/control`. Their complete module-level authorship,
  license expressions and transitive coverage have not been audited here.
  Do not label this initial list a complete inventory. Development-only tools
  and separately supplied system libraries must be identified as such.
- License-text references for the audit:
  [MIT](https://spdx.org/licenses/MIT.html) and
  [GPL-2.0-or-later](https://spdx.org/licenses/GPL-2.0-or-later.html).
  The viewer is an access route for notices; package/source obligations still
  have to be satisfied by the corresponding delivery artifacts.

## Implementation findings, 2026-09-18

Observed while implementing, not planned behaviour:

- `RenderViewport::projectionMatrix()` already accounts for the render rect's
  origin: translating by the absolute logical position times the scale puts a
  quad at that position on the output being painted, including an output that
  does not start at the origin. Verified by `overlayPlacement` in
  `autotests/render_test.cpp`, which renders a pass for an output at (200, 100)
  and requires the text at that output's own corner. The scaler already used
  this convention; it is now covered by a test rather than assumed.
- The upstreamable-folder seam for build identity is resolved with
  `__has_include("buildinfo.h")` in `upscale.cpp`. Out of tree the build adds
  `src/buildinfo` to the effect's include path; copied into KWin the header is
  absent, the guard removes the reference and the display reports the build as
  unknown. No cross-directory include or target dependency is added to
  `src/plugins/upscale/CMakeLists.txt`.
- KConfigXT stores a value equal to the current default as no entry at all.
  That is what makes build-type defaults safe: a Debug user who leaves the
  developer view on writes nothing, and a user who switches it off writes an
  explicit `false` that survives a later release build. Covered by
  `displayDefaults` in `autotests/config_test.cpp`.
- The render test needed a `QGuiApplication` for the font database once it
  measured text, so it runs with the offscreen platform.
- The integration test's driver wraps the effect and forwards the calls it
  cares about, so the effect's screen pass was never reached and the display
  went untested until the driver forwarded it too. Running that pass against
  the driver's own OpenGL target keeps the display on the tested path while
  the test compositor keeps painting with QPainter.
- Composing the display honoured every mode switch except the master one,
  which only its caller checked. Off now means off inside the class as well;
  one switch with two meanings is a bug waiting for a second caller.
- KWin excludes an effect whose `isActive()` is false from the chained paint
  methods of the next frame, which its own `effect/effect.h` states. An effect
  that refused every window would therefore never be called to say why, so the
  display keeps the effect active while it has something to show, and the
  scanout block follows that same state. No automated test covers this: the
  `activeEffects` D-Bus property reports the same value with and without the
  change, so an assertion on it would pass either way. It is verified in the
  native session instead, where the display has to appear over a refused
  fullscreen window.

## Acceptance criteria

### Settings version correction, 2026-09-19

The owner reports a footer showing only `0.` and requests
`X.X.X short_hash build_date branch/tag`. The current settings formatter uses
the package version, branch and date; the generator has no independent public
revision field and caches timestamps across build invocations. Correct the
existing identity work here, preserving package-version semantics and the
installed-versus-running comparison. Keep About, notices and rendering outside
this correction. Use the existing build generator and settings test variants.

The settings footer must show the base CMake version, abbreviated revision
(including a dirty marker when applicable), UTC build timestamp and branch/tag
in that order. Release tags must retain the hash; unavailable source identity
must be explicit. Check the generated record and the actual label, including
release, detached-tag, archive and reproducible-date cases. The supported-scope
gate is the maintained build/check matrix; native settings inspection is the
full acceptance for this footer. These checks are planned, not yet observed.

Observed for the developer trial on 2026-09-19: all eight build-information
regressions and both settings test variants passed in the maintained Trixie
container. The configured commit hooks passed for the changed files. The
Trixie GCC settings target built; a subsequent direct build refreshed the
timestamp, compiled only `buildinfo.cpp` and linked the module, with no new moc
or resource compilation. The native Release settings target built on pcjensd.
A private-bus probe loaded both the old and new libraries and rendered their
widgets: the previous installed label already contained a full snapshot
version, so a literal `0.` was not reproduced there. The new visible label is
`0.1.0 6313d6d37a-dirty 2026-09-19T12:43:20Z applications/recognize-and-request`.
The new settings module was installed on pcjensd; the previous library is
preserved under `build/settings-version/`. This was a focused build for the
owner to try. The full two-container/two-compiler matrix, both whole-tree hook
stages, clang-tidy and real-session acceptance have not run for this correction
and remain required before closing the slice.

Planned checks, not observed results:

- All required fields are present in About and the initialization log: plugin
  name, author, exact version, GitHub link, branch/tag, independent full hash,
  complete UTC build timestamp, and license identity/link. Copy preserves exact
  values and shows explicit missing/unknown states.
- Cover clean release tags, branches, dirty snapshots, detached HEAD, CI refs,
  multiple known tags, official source archives and arbitrary Git-free builds.
  A release hash remains available even without a hash in its version string.
  Package-version overrides and reproducible timestamps retain their meanings.
- Observe the generator on every build invocation, including no source edits
  and direct effect/KCM target builds. Without `SOURCE_DATE_EPOCH`, a subsequent
  build at a different time updates the timestamp and compiles only the small
  generated unit, then links affected binaries. Changing branch/tag/revision
  after configuration also updates the record without reconfiguring CMake.
  Scaler/configuration consumers and Qt metadata/resource generation must not
  rerun solely because build values changed. Record verbose build evidence.
- With fixed `SOURCE_DATE_EPOCH` and otherwise identical inputs, the generator
  still runs but leaves identical output untouched, requiring no compilation
  or link. Verify the modules agree when installed together and report an
  already-loaded older effect honestly after an upgrade.
- The settings use standard KDE interaction on the supported target, with
  accessible labels, focus and keyboard activation. Opening/closing About and
  following a details/license link leave unsaved configuration unchanged.
  About works without a game; installed data does not require effect loading.
- A successful effect initialization emits one complete identity record.
  Repaint, reconfigure and opening settings do not repeat it. Metadata-only
  inspection and KCM loading do not claim compositor initialization.
- With absent preferences, Debug enables OSD, persistent statistics and
  developer information; Release, RelWithDebInfo and MinSizeRel show only timed
  detection/basic summaries. Cover multi-configuration builds, explicit Off,
  release opt-in, Debug opt-out and persisted choices across build changes.
  Master Off suppresses all modes without disabling logging or processing.
- Exercise detection and summary timeouts, late identity, repeat suppression,
  effective-setting changes, game/output changes and close/lock cleanup. Cover
  disabled/ineligible scaling, profile events through controlled fixtures, and
  neutral application labels before actual profile integration. Verify future
  override interfaces without claiming profile integration is implemented.
- Candidate diagnostics name the specific condition that refused a window, not
  the general eligibility rule. Cover each documented reason separately, and
  verify against the fixture in `autotests/wayland_client.cpp` that a conforming
  buffer which is nevertheless refused is reported by its failing condition.
  This is the acceptance that explains the refusal recorded on real hardware in
  the [rendering slice](slice-fsr1-hdr-vrr.md); diagnostics that cannot explain
  it do not close this package, whatever else they display.
- Statistics and developer snapshots report every currently implemented
  setting/state from the handbook inventory, including origins where known,
  configured versus effective values, bypass/failure reasons and missing or
  stale values. Verify coherent window/output data, labelled FPS event sources,
  duplicate suppression, sample age and correct handling when updates stop.
- Logs and visible snapshots agree on loaded identity and state. Default
  startup identity is visible in both build types. Repeated unchanged states
  do not flood logs; debug tracing is separately controlled. Verify no complete
  launch environment or arbitrary arguments appear in routine diagnostics.
- Passive modes never take focus, consume game input or contaminate capture.
  Verify bounded updates, no synchronous readback or new full-screen animation
  loop for statistics, and release of overlay sampling and composition work
  when hidden. Output presentation sampling remains active for status reports. Check legibility, game input, SDR/HDR and VRR in a native session,
  including visibility changes, output movement and lock/unlock.
- The notices inventory covers actual incorporated shaders, libraries and
  dependencies with accurate upstream attribution and license expressions.
  Assert AMD's complete notices survive shader resource processing and appear
  in installed notices and package copyright metadata. Verify offline access,
  full texts, exceptions and any additional required notices for audited inputs.
- Validate installed packages with and without the KCM and extracted source
  archives. Check notice resources and corresponding-source access against the
  release contents; missing required attribution/license resources fail the
  appropriate existing checks rather than being silently omitted in the UI.
- Run repository checks, metadata validation, both compiler/container builds
  with warnings as errors, targeted settings/build-info/package tests, and
  native settings, logging and OSD acceptance on wzpc. This slice checks the
  diagnostic overlay's interaction with HDR/VRR; complete renderer acceptance
  remains with rendering. The optional full About overlay is not a gate.

## Progress and remaining work

- [x] Record requirements for About, identity, logging and license access.
- [x] Inspect KWin settings conventions and current build/license metadata.
- [x] Make diagnostics infrastructure the next slice and specify developer
  fields, build-type defaults and shared state/logging responsibilities.
- [ ] Verify minimum-version dialog APIs; the upstreamable data seam is chosen
  and implemented (see the implementation findings above).
- [ ] Audit exact dependency/component notices and delivery obligations.
- [ ] Implement generation, metadata and About/details access. The
  initialization log and a settings version line exist: the identity is logged
  when the effect initializes instead of when its library loads, the settings
  module links the same record, and the effect exposes it as a property so the
  page can report the loaded build beside the installed one.
- [x] Implement candidate selection and rejection reporting first, so the
  rendering slice's scaler-effective gate can be diagnosed. Every documented
  condition now has its own reason, including the paint-pass conditions and the
  refused buffer format, reported with its DRM four-character code.
- [x] Implement the state snapshot and settings diagnostics. One snapshot per
  pass feeds both the settings status and the display. Transition logging
  remains open.
- [ ] Implement transition logging on the effect's logging category.
- [x] Implement the passive OSD, the statistics and developer view and the
  build-type preference defaults. A shortcut to toggle the view, profile
  overrides and per-profile visibility remain open.
- [x] Separate the three passive displays. The timed announcement, the
  persistent view and the developer dump were one growing block in one corner,
  which is what Jens asked to have taken apart on 2026-09-19. They are now
  three blocks in three corners: announcement top left, developer information
  bottom right, and the persistent view in a corner the user chooses, stored
  as `OsdPosition` and offered on the settings page as **Frame rate position**,
  top right by default. Developer information no longer extends or enables the
  persistent view. Placement lives in `placement.cpp`, which also stacks two
  displays sent to the same corner and keeps a display larger than its output
  from starting off the screen. The handbook states this in
  [four displays, four places](../upscaling.md#four-displays-four-places).
- [x] Make the persistent view a heads-up display: frames per second, frame
  time, 1% low and what the picture is drawn at, in the terms every frame-rate
  overlay uses, at 1.6 times the session's font size. Asked for by Jens on
  2026-09-19, because the first version showed everything at once in one size
  and could not be read at a glance mid-game. The detail it carried was
  already in the developer view, so `upscaleStatistics` is gone rather than
  duplicated; the handbook states the contract in
  [the heads-up display](../upscaling.md#the-heads-up-display).
- [x] Explain and fix the developer display Jens saw during Extreme Tux Racer
  on 2026-09-19, reported as detached from the application and at one point
  behind it. Every block is drawn after `effects->paintScreen`, so nothing is
  wrong inside a composited frame; what was wrong is the frames that never
  came. KWin calls no paint hook of an inactive effect, and a screen repaints
  only what was damaged, so the blocks stayed in the framebuffer after the
  game they described was gone, and windows repainting over them made them
  look like they were behind. The display now records whether it reached the
  screen and asks for one more frame when it hides, and the effect hides it
  when its window closes, stops being the one on screen, leaves fullscreen or
  the session locks. The corners this change moved two blocks into are over
  the wallpaper, which is why it showed up now.
- [ ] Test what a game that never exits cleanly leaves behind, asked for by
  Jens on 2026-09-19: `kill -9` on a running game, repeatedly, watching that
  nothing grows. Covered by the fix above for the display's own textures and
  by [resolution control](slice-resolution-control.md) for what was requested
  of the client. Not yet written; a nested-session test can kill a client
  between frames, and a native check should watch process and video memory
  across repeated launches.
- [x] Follow the session's font settings and the output's scale factor in the
  OSD. The family and the size now come from the session's fixed-width font,
  and the size is multiplied by the scale factor of the output the text is
  drawn on. A point size is converted at the 96 dpi reference KDE scale
  factors are stated against.
- [ ] Re-lay out when the session's font settings change. A changed family or
  size applies to the next layout, which happens when the text or the scale
  changes; nothing watches the settings themselves.
- [ ] Complete automated, package and native acceptance; preserve lasting design
  in source/human documentation before removing this slice.

Heads-up display follow-up, 2026-09-19: finish validation of the compact view.
The current formatter calls every bypass native, even with a smaller supplied
buffer, and abbreviates resolutions using only their height. Restrict native
to observed equal input/output sizes and common names to their exact sizes;
show other bypasses with the observed dimensions and FSR off. Keep unavailable
timings as dashes. Cover these cases and the larger text, then run the required
container/compiler, hook and static checks. These are planned checks. The
reported developer-display ordering problem still needs reproduction in the
real session; drawing last in one screen pass alone does not explain it.

Documentation validation, 2026-09-18: `pre-commit run --all-files` and the full
pre-push stage passed in the Trixie container on an isolated copy under
`build/about-spec-check`, containing the committed implementation and current
documentation. Local links and heading anchors resolved. These checks cover
the specification; no About implementation, dependency-license audit,
incremental-build acceptance or native dialog/log test has been performed.
This recorded result predates the expanded diagnostics scope and slice rename.

Implementation validation, 2026-09-18: built in both containers with GCC and
with Clang, warnings as errors, and `ctest` passed in each: six tests in Trixie
(KWin 6.3.6) and four against KWin master, where the integration test is not
built. New coverage: each refused window reported by its own condition in
`autotests/integration_test.cpp`, each size relation in
`tools/upscale-resolution-test.cpp`, overlay placement and resource release in
`autotests/render_test.cpp`, and build-type display defaults in
`autotests/config_test.cpp`. Both pre-commit stages and clang-tidy passed in
Trixie. No native session acceptance has been performed yet: legibility, game
input, HDR/VRR behaviour and lock/unlock on wzpc remain open, and so does the
refusal this package exists to explain.

Expanded documentation validation, 2026-09-18: both pre-commit stages passed
in Trixie on the isolated documentation candidate under
`build/documentation-commit-check`. This includes the checker regressions and
REUSE licensing check. Local paths and heading anchors resolved in all 13
documentation files. No feature implementation or runtime acceptance is claimed.

Lifetime work, 2026-09-19: three things outlived the game that needed them.
The scaler holds two textures and their framebuffers at the game's resolution,
which is tens of megabytes of video memory at 4K, and kept them for the rest
of the session; the list of windows refused for their colours kept an entry
per dead window; and the mode override kept an announcement per client that
had been told a smaller mode, including clients that were killed. All three
are released now, on the same events that end the display, and only when no
window anywhere can still use them. Whether anything else grows across
repeated crashes is the open test above.

Display separation validation, 2026-09-19: built natively with GCC, warnings as
errors, and the tests the change calls for passed: `upscale-display`,
`upscale-display-gles`, `upscale-config`, `upscale-config-buildinfo`,
`upscale-application-editor`, `upscale-snapshot`, `upscale-render`,
`upscale-render-gles`, `upscale-render-shape`, `upscale-render-shape-gles`,
`upscale-framestatistics` and `upscale-resolution`. New coverage:
`blocksKeepTheirOwnCorners` in `autotests/display_test.cpp` asserts which
corner each display landed in, that moving the persistent view moves nothing
else, that the developer dump stands alone when the persistent view is off,
that two displays in one corner stack with a gap instead of overdrawing, that
twice the output scale covers about four times the area, and that a display
larger than its output keeps its beginning on screen; `displayDefaults` in
`autotests/config_test.cpp` covers the new control, its dependence on the
persistent view, and storing, reloading and restoring the chosen corner. This
was a build for Jens to try: the container builds, Clang, clang-tidy and both
pre-commit stages have not been run for this change, and no native acceptance
of the new placement has been recorded yet.

### PR #8 coverage follow-up

Hosted run `35338192292` failed the unchanged 90% C++ line gate: 744 of 1079
lines were exercised (69.0%). The new snapshot formatter had 14.9% line
coverage, display policy 35.6%, and eligibility diagnostics 52.1%; existing
compilers, lint, sanitizers and package smoke checks passed. Work proceeds in an
isolated checkout so the implementation checkout stays available.

Add behavioral tests for snapshot accuracy, refusal-specific explanations,
unknown measurements, configured versus effective state, and display visibility
and sampling. Exercise settings status through an isolated bus if needed, and
ensure the configured runtime-test entry point includes the new tests. Preserve
the coverage denominator and 90% threshold. Validate coverage in Trixie, both
compiler/container combinations and both repository hook stages before pushing
the fix, then process hosted checks and review for the latest PR revision.
These automated checks do not establish native display acceptance.

Review follow-up: distinguish a missing window from a missing buffer; include
the rejected buffer format in every diagnostic view; restore all blend factors
after painting the overlay; clear stale running-build identity on missing or
failed status replies. Display counters now belong to one observed window and
reset on window changes, hiding and reconfiguration. Timed notices release the
composition requirement when they expire, and selection is reused only inside
one synchronous screen paint. A private D-Bus service and the virtual KWin
lifecycle exercise these paths without contacting the user's desktop.

Trixie coverage passed after extending the runtime tests. Final compiler, lint,
static-analysis and hosted validation for this correction remain pending.

Integrated the concurrent diagnostic-test commit `fd7eb20`, retaining its parser
extraction and distinct assertions. Display tests have their own executable and
isolated settings in each process, including the separate OpenGL ES run. The
virtual KWin driver exercises the complete screen pass, including selection
reuse, while preserving the real scene's paint chain. OpenGL ES framebuffer
readback uses matching floating-point pixels; throttling assertions account for
observed initialization and scheduling time.

The combined production code built with GCC and Clang, warnings as errors, in
Trixie and Neon. Runtime tests passed with both compilers: eight in Trixie and
five in Neon. Trixie clang-tidy and metadata validation passed. Coverage measured
1049 of 1133 lines (92.6%), above the unchanged 90% gate. Both hook stages and coverage passed after
the test-timing adjustment and integration of the current branch and master.
Hosted checks and review for the published correction remain pending.

Review `5247444353` found that the extracted status parser retained an earlier
identity when its caller reused the output string. Clear the optional output
before parsing and test both a missing property and an empty report using the
same string. Exercise the expiry callback without a compositor as well, covering
the guard added in `33ce7f4`. These follow-ups passed the checks below.

The additional lifetime audit reproduced a title-change regression: after a
notice expired, changing the same window's caption restarted it. The focused
`visibilityAndSampling` test failed at its new expiry assertion before the fix.
Key the deadline to window identity, while refreshing caption text independently;
this preserves the documented no-repeat behavior for changing game titles.
The final combined follow-up passed the checks below.

The next review confirmed the title regression and identified a second identity
issue: matching only the version hid differences in branch, build date or Qt.
Compare the complete reported identity and exercise the settings tests both with
and without the generated build information. The matching case and same-version
rebuilds are covered through the private D-Bus service. The translation slice
also now names metadata alongside catalogues when adding a language.
The complete follow-up passed both hook stages, GCC and Clang with warnings as
errors in Trixie and Neon, all nine Trixie and six Neon runtime tests, Trixie
clang-tidy and plugin metadata validation. Coverage measured 1057 of 1136 lines
(93.0%), above the unchanged 90% threshold. Hosted CI and the next review of this
revision remain pending; real-device acceptance remains open.

Hosted CI run `35344183917` passed every job and the Quality gate for `127d318`.
CodeRabbit marked the four latest findings resolved and reported no new findings,
but skipped all ten changed files as similar to previous changes. Its latest
formal review still requests changes on `33ce7f4`; the approval gate therefore
correctly remains pending. The eight earlier review threads also remain open
although their fixes and regression coverage are published. A fresh full review
has been proposed to the owner; permission to post that request is pending.
No review override, merge or change to protection has been performed.

PR #14 review follow-up: resetting presentation measurements with a null output
now clears old samples even after the QPointer was cleared by output destruction.
Presented-frame text uses plural-aware translation. The targeted tests and
combined-candidate checks are being rerun before publication.

## Absorbed topics

Two packages that were specified but never started were merged into this
document on 2026-09-20, because each one continues a topic this package already
owns rather than opening a new one. Interactive controls extend the same
on-screen surface as the passive displays, and the languages the texts ship in
follow the texts themselves. Nothing was closed by the merge: every open item,
gate and acceptance criterion below is carried over unchanged.

## Interactive in-game controls

### Start state

At the start of implementation, the passive surface described above has
provided the shared passive OSD, timed detection/basic summaries, statistics,
developer information and diagnostic state. Interactive controls, comparison
remain specified but not implemented. Managed restart was superseded by the
2026-09-19 plugin-only requirement and is excluded.

### End state

The existing overlay offers an on-demand settings panel that distinguishes
live and pending changes, explains when a normal new launch is needed, and permits a
temporary visual comparison without altering saved settings or supplied input.
Required automated and real-game/TV acceptance has passed.

### Scope and boundaries

Own interactive settings actions, input/focus handling, comparison and the
presentation of pending settings. Reuse the diagnostic surface and state
described earlier in this document and the profile/resolution contracts.
Do not reimplement passive statistics, build defaults, detection or logging.
Future filters, geometry modes and display-specific overrides do not expand
this package's completion gate. Optional full About access may reuse the
shared identity/notices, but it is not required to close this package.

#### Dependencies

The passive surface comes first, within this same package.
[Profiles](slice-application-profiles.md)
provide matching and resolved settings;
[resolution control](slice-resolution-control.md) supplies live capabilities
and actual results. UI checks may use controlled states, but closing this slice
requires integrated live/pending-setting behaviour and real-game input acceptance.

### Approach

1. Extend the existing overlay with an interactive mode, sharing configuration
   and diagnostic state with the settings module and passive views.
2. Apply verified live changes and show pending ones without automatic restarts.
   Keep global edits, explicit profile overrides and Use global distinct.
3. Report settings that apply on the next normal launch. The effect does not
   launch, close or restart processes.
4. Add temporary comparison at unchanged input/destination geometry; verify
   input, focus and pointer restoration, cleanup and HDR/VRR interactions.

The permanent handbook defines [in-game controls](../upscaling.md#in-game-controls-and-applying-settings)
and [restart behaviour](../upscaling.md#restarting-a-game-with-pending-settings).
A window match cannot reconstruct a launch, and launch management is excluded.
Saving settings never restarts a running game. These are requirements, not observed behaviour.

### Acceptance criteria

Planned checks, not observed results:

- Apply live changes without restarting; cover mixed live/pending changes,
  reverting pending values, profile inheritance and explicit save scope.
  Ordinary Apply never restarts a game. Disabled/ineligible games retain
  controls; closing the panel restores focus and pointer state.
- Interactive edits update the existing diagnostic snapshot and timed basic
  summary consistently. Detection/statistics/developer visibility and configured
  preferences survive opening and closing the panel. Passive content remains
  outside capture and takes no input after the panel closes.
- Comparison preserves saved settings and supplied resolution while switching
  paths; if split view is provided, both halves use the same source frame.
- Explain bind-time changes without claiming they changed the running client.
  No control may start a launch wrapper or relaunch the game.
- Run repository checks and relevant rendering/configuration/integration tests
  with GCC and Clang on both container targets. On wzpc, verify legibility,
  live/pending settings, game input and HDR/VRR during and after interaction.

### Progress and remaining work

- [x] Specify live controls, pending restart settings and comparison.
- [x] Assign the passive OSD, metrics and developer defaults to the passive
  surface above so this topic has one completion gate.
- [ ] Integrate interactive controls with profiles and diagnostic state.
- [ ] Implement comparison and pending-setting presentation.
- [ ] Complete required checks and real-session acceptance.

Overlay/restart documentation validation, 2026-09-18: `pre-commit run --all-files`
and the complete pre-push stage passed in Trixie on an isolated copy under
`build/overlay-geometry-check`, containing the committed source and updated
documents. Local documentation links and heading anchors resolved. No overlay
or restart implementation or runtime acceptance was performed.
This recorded documentation result predates the split into work packages,
and the 2026-09-20 merge of that split back into this document.

## Shipping the texts in every language

### Start state

The code already calls KI18n for every user-visible string, and the build
defines the translation domain `kwin_effect_upscale` for the effect and for the
settings module. Nothing else exists: there is no message template, no
catalogue directory, no catalogue installation, and no translated plugin
metadata, so every user sees English regardless of their session language.

Several strings are also composed rather than written as whole sentences. A
refusal reason is a clause that appears inside three different frames — the
settings status, the timed summary and the developer view — and carries no
`i18nc` context saying so. A translator cannot see the finished sentence, and a
language that orders it differently cannot produce a correct one.

### End state

A user who installs the package sees the effect in their own language.
English, German, French and Spanish are shipped and complete for every
user-visible string, the effects list shows a translated name and description,
and a further language is added with a catalogue and translated plugin metadata.
Composed strings carry the context a translator needs. The
[handbook's language requirements](../upscaling.md#language-and-translations)
are the permanent specification.

### Supported scope and full acceptance

**Supported scope.** The four languages above, complete for the settings page,
the on-screen display and the status texts, verified in a session for each one.
This is what the package can be released with.

**Full acceptance.** Additional languages as they are contributed, review of
each translation by someone who speaks it, and the layout checks repeated on
the television for the display's longer strings.

### Scope and boundaries

Own the extraction template, the catalogue layout and installation, the
translated plugin metadata, the packaging of compiled catalogues, and the
`i18nc` context for composed strings. Own the language acceptance runs.

Do not change what the texts say: rewording belongs to the package that owns
the text. Do not add a language selector; the session's language decides.
Right-to-left layout is not required yet and must not be claimed. Machine or
agent-produced translations are a starting point for review, never a claim that
a language has been checked by someone who speaks it.

#### Dependencies

The display and status texts this topic translates are owned by this same
document, which is why the two were merged: a text and its `i18nc` context are
now written once, by one owner, instead of being translated twice. The
[build and release pipeline](slice-build-release-pipeline.md) owns the package
contents that the compiled catalogues become part of.

### Approach

1. Add `Messages.sh` in KDE's form and generate the template, covering the
   effect, the settings module and any string in the plugin folder.
2. Give every composed string `i18nc` context that names the frames it appears
   in, and split any string whose grammar cannot survive reordering.
3. Create `po/` with catalogues for German, French and Spanish, install them
   with `ki18n_install(po)`, and confirm the packages carry the result.
4. Add translated `Name` and `Description` entries to the plugin metadata, and
   check that the effects list shows them.
5. Run each language in a session and record what was observed.

### Acceptance criteria

Planned checks, not observed results:

- The template is regenerated from the current sources and contains every
  user-visible string, including the refusal reasons and the developer view.
- Each shipped catalogue is complete; an incomplete one fails the check rather
  than silently falling back to English in the middle of a sentence.
- The settings page and the display are read in each language in a session.
  German strings, which are the longest, do not break the settings layout or
  push the display off the output, at desktop scale 1 and at a scaled desktop.
- The effects list shows the translated name and description with the plugin
  not loaded.
- An installed package supplies the catalogues; a source archive build produces
  them as well. Removing the settings module does not remove the effect's own
  translations.
- Pixel counts stay ungrouped in every language, and dates and decimals follow
  the locale.
- Repository checks and both container builds pass with the catalogues in the
  build.

### Progress and remaining work

- [x] Record the language requirements in the handbook, as requested on
  2026-09-18: KDE conventions, the four required languages, composed-string
  context, and the locale exception for pixel counts.
- [ ] Add the template, the catalogue layout and the installation.
- [ ] Give composed strings their context and split what cannot be reordered.
- [ ] Translate German, French and Spanish, and have each read by someone who
  speaks it.
- [ ] Translate the plugin metadata.
- [ ] Run and record the per-language session acceptance.
