<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: development infrastructure and diagnostics

## Priority

This is the next implementation slice, as requested on 2026-09-18 and confirmed
after the first hardware runs. Its single topic is unchanged: making the current
effect identifiable and observable during development through About and build
identity, notices, logging, and the shared passive OSD with developer
information. Existing rendering and pipeline acceptance records remain open
until their own gates pass; their outstanding work is not absorbed here.

The package now carries a second purpose that fixes its order. The effect has
never been observed scaling a frame. The
[rendering slice](slice-fsr1-hdr-vrr.md) recorded a supplied buffer that meets
every documented eligibility rule and that the effect refuses on the real
session, without reporting which condition failed. Nothing in the product can
currently answer that question, and the passing container tests did not predict
it. This package builds the instrument that answers it.

That makes the ordering deliberate rather than incidental: diagnostics first,
so that the rendering slice's
[scaler-effective gate](slice-fsr1-hdr-vrr.md#the-scaler-effective-gate) can be
closed immediately afterwards. Work inside this package is sequenced to serve
that. Candidate selection and rejection reporting come before About, notices and
the overlay presentation, because the next gate depends on them and the rest
does not.

The obligation runs both ways. This package is not complete because a dialog
renders and a log line appears. Its diagnostics have to be good enough to
explain an actual refusal on real hardware, which is the acceptance recorded
below.

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
game-setting controls, comparison and restart actions remain in the later
[game controls slice](slice-game-overlay.md). Game-profile implementation,
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
game/profile events and per-profile overrides to the existing OSD; the controls
package later extends the same surface. Test those interfaces with controlled
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
  loop for statistics, and release of diagnostic sampling and composition work
  when hidden. Check legibility, game input, SDR/HDR and VRR in a native session,
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
- [ ] Follow the session's scaling and font settings in the OSD, per output,
  and re-lay out when either changes. The text already follows the output's
  scale factor; the family and size are the effect's own choice today.
- [ ] Complete automated, package and native acceptance; preserve lasting design
  in source/human documentation before removing this slice.

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
