<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: the applications we know about

## Current status

Matching, layered catalogue storage and an editor are implemented. The shipped
catalogue has four entries: SuperTuxKart, Extreme Tux Racer, glmark2 and vkmark.
The editor supports manual/window-based addition, field editing, disabling
shipped entries, deleting user entries and restoring defaults. Native is a
scaling opt-out; Enabled controls matching participation. MinimumPixels can
inherit the global threshold or override it, including zero to disable it.

General sparse overrides for all effect/OSD settings and editor reordering
remain open, along with full real-session acceptance. The early model below is
a proposal; the later layered-configuration and editor records describe what
was implemented.

## Historical start state

The effect initially had global settings only. Matching, profile storage and the profile
editor were not implemented. Source investigation established reusable KWin
window detection and the sparse setting model below.

## End state

Users can create, inspect, edit, delete and reorder application profiles, match
the correct game across supported window backends, and override selected
settings while all other values follow globals. Editable known-application
templates have observed identities and justified defaults. Persistence,
matching, editor and real-session acceptance have passed.

## Scope and boundaries

Own identity selection, matching priority, sparse overrides, storage, the
configuration editor and known-application templates. Share resolved identity
and settings with consumers; do not give each consumer a different matcher.

The [development infrastructure](slice-development-infrastructure.md) owns the
passive OSD and detection-message lifecycle. This profile package supplies real
match events and sparse OSD overrides, and owns their integrated acceptance.
The [interactive controls](slice-development-infrastructure.md#interactive-in-game-controls)
own the in-game panel and comparison.
Managed launch/restart is superseded; the handbook records that decision under
[application launch configuration](../upscaling.md#application-launch-configuration-and-method-discovery).
[Resolution control](slice-resolution-control.md)
owns obtaining smaller buffers. Those packages must not prevent closing this
profile package once its own end state is accepted.

## Dependencies

Use KWin's existing detection service and current global configuration. Expose
resolved profile/settings and identity-change notifications to the diagnostic
OSD and later consumers. Validate profile events and overrides with that OSD;
its rendering and the consumers' process implementations are outside this package.

## Approach

Implement the identity and sparse-persistence model below, then the editor and
runtime invalidation. Validate application templates from observed windows.
Test globals and overrides through the actual rendering decision, and record
remaining cases in this document until acceptance is complete.

## Source findings

Inspected the existing source copies under `build/upstream/`: KWin v6.3.6 at
`b8de4329447824b1b1e7a36b3a57acfd069f1423` and the cached master revision
`7db8e19725edad20135d8a63a19ae52e0a3ef4b9`. These findings describe those
revisions, not an assertion that the cached master is the latest upstream.

### Window detection is already available

KWin's Window Rules editor calls `org.kde.KWin.queryWindowInfo` on `/KWin`
through the session bus. KWin performs interactive window selection and returns
the class, instance, desktop file, caption, role, type and window UUID.
Cancellation and unmanaged windows have distinct error replies. The same
service exists in both inspected revisions. Despite the caller's name
`selectX11Window`, selection is implemented in KWin and is not limited to X11.
Use this service from the existing Widgets configuration module; an X11-only
window picker or a process scanner is unnecessary.

Sources:
[Window Rules detection](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/kcms/rules/rulesmodel.cpp),
[D-Bus implementation](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/dbusinterface.cpp),
[master D-Bus implementation at the inspected revision](https://invent.kde.org/plasma/kwin/-/blob/7db8e19725edad20135d8a63a19ae52e0a3ef4b9/src/dbusinterface.cpp).

Neither inspected reply includes an explicit Wayland/X11 discriminator. Do
not infer that from the desktop file or instance. The initial matcher can use
KWin's common class and optional instance fields across both protocols. If
protocol-specific matching is added, obtaining the protocol in the editor
needs an explicit solution; runtime has `EffectWindow::isWaylandClient()` and
`isX11Client()`.

Inside the effect, `EffectWindow::window()` exposes `Window::resourceClass()`
and `resourceName()` separately in both targets. Native xdg-shell application
IDs are assigned to `resourceClass()`; X11 supplies the two `WM_CLASS` parts.
`EffectWindow::windowClass()` concatenates instance, a space and class. Read
the separate fields rather than parsing that string. `windowClassChanged`
provides invalidation when identity arrives late or changes.

Sources:
[native application ID handling](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/xdgshellwindow.cpp),
[X11 class handling](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/x11window.cpp),
[effect window access](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/effect/effectwindow.cpp),
[identity change notification](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/window.cpp).

### Existing application exceptions and rules

A case-insensitive search for `kodi` and `xbmc` found no occurrences in either
inspected KWin source tree. This does not rule out examples elsewhere in KDE
or older versions. A concrete reusable example is Glide: it tests Plasma's
window classes and an exclusion list before deciding which windows to animate.
This demonstrates application-specific behaviour within an effect, but its
hard-coded list does not provide the requested user editing or inheritance.
[Glide source](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/plugins/glide/glide.cpp).

KWin also handles client compositing-block requests and window rules on X11
without identifying Kodi by name. That is a possible explanation for observed
special behaviour, not a verified explanation of a particular Kodi session.
[X11 compositing handling](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/x11window.cpp).

`Rules::matchWMClass` supports exact, substring and regular-expression matches,
either on class alone or on instance plus class. Its exact comparison is case
sensitive. The rule engine has a fixed collection of compositor properties;
there is no registration point in the inspected implementation for arbitrary
effect settings. Adding FSR and sharpening properties there would require
changes to KWin's rule schema, engine and configuration module.
[Rule matching](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/rules.cpp),
[rule properties](https://invent.kde.org/plasma/kwin/-/blob/v6.3.6/src/rules.h).

## Options and recommendation

| Option | Benefit | Cost and conclusion |
| --- | --- | --- |
| Extend KWin Window Rules | One editor for all window behaviour | Requires changing KWin itself; consider for upstream integration, not a standalone effect on 6.3.6. |
| Copy the complete Window Rules machinery | Existing powerful matching and UI | Brings unrelated policies and tightly coupled implementation; unnecessary for sparse effect settings. |
| Use KWin detection with a small effect-owned profile model | Works with existing KWin and our Widgets module | Recommended. Reuse the service and matching conventions, with a dedicated sparse configuration model. |
| Use desktop entries or executable names alone | Convenient labels and installed-app browsing | Does not reliably identify a particular window, game or launcher. Use desktop entries for presentation and suggestions only. |

Call existing interfaces where possible. If source is copied, retain its
copyright and licence headers; do not replace upstream attribution with the
project's own header. A desktop entry, Steam identifier or suggested profile
does not by itself authorize future client-resolution changes.

## Early proposed design

This section records the initial design, not the implemented configuration
format. In particular, the template-only catalogue proposal was superseded by
active shipped defaults, and matching permits an instance-only constraint.

### Identity and selection

- Require a nonempty exact application ID/window class; preserve case. Allow an
  optional exact instance constraint. Label the common field clearly in the
  editor and show detected values before saving.
- Permit multiple explicit identities for one profile, for example a verified
  native ID and a verified Xwayland class. Identities are alternatives; fields
  within one identity must all match. Do not assume a Flatpak ID, desktop file
  and native ID are interchangeable.
- Start without substring or regular-expression matching. Titles change during
  play and are unsuitable as the default identity. Future role/title refinements
  must be explicit and cannot replace the required application identity.
- Order profiles visibly; the first enabled matching profile wins as a whole.
  Missing fields inherit from globals, not from later matching profiles. Show
  overlap warnings and the selected profile in status. Offer reordering.
- Keep profile participation separate from the `Enabled` setting override.
  Turning off a profile makes it stop matching; an active profile with
  `Enabled=false` disables upscaling for its application.
- Do not persist a process ID or window UUID as application identity. The UUID
  is useful only for the current selection. Unknown applications use globals.
- Matching does not relax render eligibility: unsupported surfaces, transforms,
  lock screens and other existing exclusions still bypass the effect.

### Sparse values and persistence

Keep global values in the existing `Effect-upscale` group and generated
`UpscaleConfig`. Use nested `KConfigGroup` groups for profiles with stable UUIDs,
an explicit ordered ID list and a format version. Store matching metadata
separately from an `Overrides` subgroup. Do not write to `kwinrulesrc`.

Represent each override in memory as `std::optional<T>`. A missing key means
inherit; false and zero are real overrides. Check presence before reading and
validate enum/range/type constraints. Invalid values should fall back to globals
and be reported, not silently turn into a false or zero override. Resolve a
complete settings value from globals plus the selected profile; never mutate
the global configuration while drawing a window.

Use `KConfigGroup::hasKey`, `readEntry`, `writeEntry` and `deleteEntry` for
presence and persistence. An explicit value equal to the current global value
must remain stored: it expresses an override even after the global value changes.
Choosing **Use global** removes the override key. A profile with no overrides is
valid and follows globals. The initial format needs no per-setting inheritance
flags or copies of global defaults.
[KConfigGroup API](https://api.kde.org/kconfiggroup.html).

The proposed general override layer would cover: `Enabled`, `Preset`,
`Percentage`, `Sharpening`, and `Strength`. Resolve fields first, then apply
their dependencies: `Percentage` matters for Custom, and `Strength` matters
when sharpening is enabled. Changing the profile's percentage control should
explicitly select a Custom preset override as the global editor does. Turning
sharpening off must retain any explicit strength for later use. Preset changes
must not silently save inherited percentage values.

The global `Enabled` field becomes the default, so an application can explicitly
enable upscaling when that default is off. Disabling the effect in Desktop
Effects remains the overall off switch. Runtime must therefore move the current
global `m_enabled` early return into per-window settings resolution. Status,
candidate selection and RCAS strength must use the same resolved values.

### User editing and known applications

Extend the configuration page with an application list and an editor. Support
**Add from window**, **Add manually**, viewing identities and overrides,
editing, deleting, enabling/disabling and reordering profiles. Each setting
offers **Use global (current value)** or an explicit value. Boolean settings
need three choices: Use global, On, Off. Show inherited controls without
turning them into saved overrides when another field changes.

Follow the existing Apply/Reset lifecycle: edits stay in the module until Apply,
Reset reloads saved state, and failed validation or persistence leaves the module
dirty. Reconfigure the effect only after successful persistence. Cancellation
of window selection creates no profile; prevent overlapping detection requests.
Use explicit **Clear overrides** for a profile and keep it separate from
restoring the global defaults or deleting profiles.

Recommended initial catalogue policy: ship known applications as templates
offered by **Add known application**. Adding one copies only its identities and
recommended overrides into a fully editable user profile. Runtime resolution
then has only globals and user profiles, with no hidden third layer. Deleting a
profile must survive restart and package updates; updates must not reinsert it
or overwrite edits. Automatic activation of shipped profiles, if wanted, needs
a separate decision about initial seeding and persistent deletion markers.

Do not invent Kodi or game defaults based only on their names. Each shipped
entry needs observed identities for supported packages/backends and a reason
for its settings. Native and Flatpak packaging and Proton/Wine games can expose
different identities; capture the main window, not just its launcher. Initial
catalogue contents remain to be measured.

### Runtime integration

Load and validate profiles on reconfiguration, not in the paint path. Resolve
settings for existing and newly added windows, invalidate on identity changes,
and repaint after profile/global changes. Clear per-window state on destruction.
If resolved settings are cached, invalidate them when either profiles or globals
change. No disk access, process inspection or regular-expression compilation
belongs in a frame callback.

Preserve the current restriction to one eligible candidate; define candidate
eligibility using each window's effective `Enabled` value. Global disable plus
one enabled profile must work. Do not carry one application's sharpening into
another. Profiles configure the supplied-buffer scaler and resolution guidance;
they do not implement resolution negotiation or prove a requested size was used.

## Observed identities and the first shipped catalogue, 2026-09-18

Jens asked for detection to be taken seriously and for entries covering the two
games installed on the development machine. Both games were run against an
unpatched KWin 6.3.6 and their identities read off the running windows rather
than derived from their names. Details of the runs and of the resolution
mechanism are in the
[resolution-control slice](slice-resolution-control.md#plugin-only-control-of-a-game-the-user-started-2026-09-18).

| Application | Package | Backend | `resourceClass` | `resourceName` | Desktop file | Executable |
| --- | --- | --- | --- | --- | --- | --- |
| SuperTuxKart | `1.4+dfsg-5+b1` | native Wayland | `supertuxkart` | `supertuxkart` | `supertuxkart` | `/usr/games/supertuxkart` |
| Extreme Tux Racer | `0.8.4-1` | Xwayland | `Extreme Tux Racer 0.8.4` | `etr` | empty | `/usr/games/etr` |

Two results change the model specified above.

Extreme Tux Racer's window class contains its version number. An entry matching
the class would stop matching at the next package update, so this entry
constrains the instance name alone. The specification's requirement of a
nonempty exact window class is therefore too strong: the requirement is that at
least one stated field is exact, not that it is the class.

A second identity is needed for the resolution mechanism. It acts when the
client binds the output, before any window exists, so no window class is
available; the identity there is the executable path KWin resolved for the
connection, compared by file name. This is KWin's own `ClientConnection`
accessor, not process inspection, and the specification's prohibition of
process scanning still holds. A profile that selects a launch-independent
method therefore needs a program field alongside its window identity.

At that first observation: `UpscaleApplication`, a code-owned catalogue with these two
entries, matching by window identity and by program, and the reports that name
a recognized application. Shipped entries are active on installation rather
than offered as templates, as decided by Jens and recorded in the
[handbook](../upscaling.md#per-application-overrides). The user-editable
profile editor was still to build at that observation; its later implementation
is recorded below. General effect-setting overrides remain open.

Unit tests cover catalogue integrity, both observed identities, case
sensitivity, a changed Extreme Tux Racer version and program matching by file
name. Nothing here has been accepted in a real session.

## The application list as layered configuration, 2026-09-18

Jens asked how the shipped list should be stored, and whether it should be
copied into the user's profile on first run or by a reset button. It is neither:
the list is a KConfig file whose defaults the package installs and whose user
changes sit in a second file layered over it.

### Why not a copy

A copy stops receiving corrections the moment it is made. During a period of
frequent packages, which is what is expected, a user with a copied list would
have to press restore after every update merely to receive new applications,
and that press would also discard the entries they added themselves. The
layered form gives them both: what they never touched follows each package,
what they changed stays changed.

### What KDE already provides, measured rather than assumed

`/etc/xdg` holds the configuration defaults of a dozen KDE applications on the
development machine, and `konqautofiltersrc` is one of them shipping a *list*
of named entries with `Enabled` and `Position` fields, which is the shape
needed here. KWin's own per-application rules are KConfig groups in
`kwinrulesrc`. JSON in KDE is plugin metadata, not a user-editable list.

Measured against KConfig 6.13 on 2026-09-18, with a system layer and a user
layer in temporary directories:

| Asked | Observed |
| --- | --- |
| Group list across both layers | Merged: shipped-only, user-only and shared entries all appear |
| A key the user set | The user's value wins |
| A key the user did not set | Still the shipped value, in the same group |
| `Key[$d]` in the user file | The shipped key is hidden, and stays hidden across updates |
| `KConfigGroup::hasDefault(key)` | True exactly for entries the shipped file describes |
| Removing the user's line | The shipped value returns |

The last two rows are what restoring is built from, and the fourth is a trap:
`deleteEntry()` writes the `[$d]` marker, which **suppresses** the shipped
value instead of restoring it. A restore implemented with it would leave the
user with nothing where the default should be, and would keep hiding every
later package's value as well. Restoring uses `revertToDefault()` for a field
the shipped file describes and `deleteEntry()` only for one it does not. An
autotest asserts the restored values and fails when the two are swapped.

### What is implemented

`kwinupscalerc`, installed to `KDE_INSTALL_CONFDIR`, holds the four measured
applications with their identities, method, preset, order and note. The effect
and the settings module share one reader, which caches the list and re-reads it
on reconfiguration so that no frame touches the disk. The reader drops an entry
that constrains no identity, and reads a method or preset it does not know as
the one that asks for nothing, so a file from a later version cannot make this
build act on a method it has not implemented.

The settings page reports whether the list still matches the shipped one, and offers **Restore the shipped
application list**, which asks first, then reverts the user's fields, removes
their own entries and tells the running effect to read again. It is separate
from the page's Defaults, which restores the effect's settings and leaves the
list alone.

Observed on 2026-09-18 with the production plugin in a nested KWin 6.3.6 at
3840 × 2160 and desktop scale 3: with the defaults alone, SuperTuxKart supplied
2560 × 1440 and the effect reported FSR 1. With a user file containing only
`[Application-supertuxkart] Preset=Performance`, the same game supplied
1920 × 1080 and the shipped method still applied, which it had to, or nothing
would have been requested at all.

### The editor, and its tests, 2026-09-19

The settings page now carries `UpscaleApplicationEditor`: the list, the fields
behind each entry, adding by hand or from a window KWin picks, removing an
entry the user added, and the restore, which is separate from the page's own
Defaults because the two are different kinds of data.

`upscale-application-editor` drives that page rather than the class behind it,
against the catalogue this build installs, so that an entry which stops parsing
or a page that cannot show one fails here. It covers selecting an entry and
reading its fields, editing through the controls a person actually uses, the
list check box and the details check box following each other, holding edits
until Apply, storing only the fields that differ, adding and removing an entry,
the three replies the window picker can give, including cancellation, and
restoring with both answers to the confirmation. `upscale-application` covers
the same storage rules without a page: the key spellings both layers use, a
method from a later version, identifiers for new entries, deleting an entry
the user added against disabling one this build ships, and the unlisted-
application setting. Coverage of `applicationeditor.cpp` is 98.3% of lines and
of `application.cpp` 98.9%.

### What the tests and the review found, 2026-09-19

Three defects in the editor and its page, each of which loses or hides an edit:

- Two applications added before Apply both took the identifier `application`,
  because it was checked only against the stored list. Applying them wrote both
  into one configuration group and one was written over the other. The
  identifier is now generated against the editor's pending entries as well.
- An entry stating neither a window class nor an instance was written and then
  dropped by the next read, so it disappeared from the list without saying why
  and left a group behind that nothing described. The editor now refuses to
  write one, names it, selects it, and leaves the page applicable.
- Reset restored the settings above the list but left the pending application
  edits on screen, where a later Apply would write what had just been
  discarded. It reloads the list with everything else now.

And one in the matching itself. A program whose entry the user had switched off
fell through to the unlisted-application setting, so with that setting on it was
asked for a resolution anyway, which is the only thing switching it off was
supposed to prevent. Being listed and switched off is now distinguished from
not being listed: the first asks for nothing, the second follows the setting.

### Still open

Sparse per-setting overrides, profile ordering in the interface and the notes'
translation, which needs the localized-entry extraction KDE uses for `.desktop`
files, all remain part of this slice.

How the shipped list grows beyond the applications one machine can run is
tracked separately below, under
[submitted applications](#submitted-applications-and-the-list-we-maintain):
producing a submittable report, the route people send it by and the rule for
accepting one. That topic builds on the storage, matching and editor implemented
here and must not hold this part of the package open.

## Acceptance criteria

Planned checks, not observed results:

- Unit tests: exact class/instance matching, aliases, case differences, missing
  identity, first-match ordering, disabled profiles and no-match fallback.
- Persistence tests: absent/false/zero, explicit values equal to globals,
  round trips, clear override, deletion, invalid values and future format
  handling. Changing globals updates only inherited fields after reload.
- Editor tests: create/read/update/delete, reorder, detection cancellation and
  errors, Apply/Reset, inherited controls, and dependent preset/sharpening values.
- Runtime tests on both container targets: native Wayland and Xwayland identity,
  late class changes, restarts/title changes, switching between applications,
  globals disabled with a per-app enable, and per-app disable. Confirm status
  agrees with the actual rendering decision.
- Before calling the implementation built: GCC and Clang with warnings as errors
  in Trixie and neon-unstable, plus the repository checks and relevant rendering
  regression tests. Real-session acceptance on wzpc must cover actual games,
  launchers and Kodi where available, and persistent user overrides on the TV.

## Progress and remaining work

- [x] Inspect global configuration and supported KWin source revisions.
- [x] Compare reuse options and specify sparse inheritance and editing.
- [x] Observe the catalogue identities of both test games and ship them.
- [ ] Validate the recommended values on real applications in a real session.
- [x] Implement catalogue model, layered persistence, editor and preset/pixel policy.
- [ ] Implement general sparse effect/OSD overrides and editor reordering.
- [ ] Run acceptance tests, both compiler/container builds and TV checks.

Observed documentation validation, 2026-09-18: `pre-commit run --all-files`
passed in the Trixie container on an isolated working-tree copy under
`build/application-profiles-check`, including this new document. The first run
flagged one wording choice in codespell; it was corrected before the passing
run. No profile implementation, build or runtime test was performed for this
investigation. Retain this slice document until implementation and required
acceptance are complete.

PR #14 review follow-up: window selection now uses an asynchronous D-Bus watcher
owned by the editor, with duplicate selections suppressed while waiting. The
catalogue test now explicitly requires the shipped entry before comparing its
program matches. Revalidation is recorded with the combined PR candidate.

## Submitted applications and the list we maintain

Merged into this document on 2026-09-20. It is the same subject as the rest of
the package — which applications we know about and what settings they get — and
it already disclaimed owning the storage, matching and editor it builds on.
It was never started. Nothing here is closed by the merge; it keeps its own
gates and its own open items below, including the four decisions still waiting
on Jens, and it does not hold the implemented catalogue work above open.

### Start state

The effect recognizes four applications, described in the
[handbook](../upscaling.md#the-recognized-applications-shipped-with-this-effect).
Every field in them was read off a running instance on one development machine,
and two of the four are benchmarks. The games this effect exists for — the ones
behind Proton, behind a launcher, or simply not installed here — are absent, and
they cannot arrive by the route the present four took: that route is one person
installing a game, reading its identity, trying the methods and measuring the
result, and it scales exactly as far as that person's disk and time.

The half that faces the user already exists. `kwinupscalerc` layers a user's own
file over the installed one, the settings page carries
[an editor](slice-application-profiles.md#the-editor-and-its-tests-2026-09-19),
and someone who works out what their game needs can make the effect do it on
their own machine. Nothing carries that work any further. There is no statement
of what a usable entry contains, no place to send one, and no rule saying what
we do with an entry for a game we cannot run ourselves.

Three things the person doing that work does not have:

- **The program behind the connection.** The advertised-mode and advertised-scale
  methods act when the client binds the output, before any window exists, so they
  match on the file name of the executable KWin resolved for that connection. The
  window picker the editor uses, KWin's `queryWindowInfo`, does not report it;
  `applicationeditor.cpp` fills the class and the instance from the reply and
  leaves the program field focused for the person to type. They will type
  something plausible, and an entry that looks complete will ask for nothing. The
  effect is the only party that can read it, through
  `ClientConnection::executablePath()`, which is what `modeoverride.cpp` already
  matches against. For an X11 client there is nothing to read: every X11
  application arrives on Xwayland's single connection, which is why
  [that method cannot address one Xwayland game](../upscaling.md#telling-one-application-that-its-screen-is-smaller).
  A report has to say which of the two cases a window is in.
- **A defined trial.** Whether a program follows a method is a fact about how it
  decides what to render, not a preference, and the only general way to establish
  it is to ask and look. The pieces are there — the editor writes an entry, the
  effect re-reads on reconfiguration, and the developer information names the
  method, the advertised size, the supplied buffer and the X11 request or its
  failure — but nothing says what order to try them in, what counts as success,
  or that a method which appears to work while the image no longer covers the
  screen has failed.
- **Something to send.** No part of the effect produces the text an entry is made
  of. The settings page shows KWin's support information as a status line; it
  contains none of the identity fields and is not meant to be pasted anywhere.

### End state

A person who got a game working can produce a complete, checkable entry for it
without reading our source, send it in one place, and see it in a later package.
We have a written rule for accepting, refusing and recording such an entry, a
test that fails when an accepted entry is missing what the rule requires, and a
route back for an entry that has gone stale. The list grows from measurements
other people made, and each entry says what was measured and under what
conditions, so a later mismatch can be traced instead of guessed at.

### Supported scope and full acceptance

**Supported scope.** The route exists and has been walked end to end at least
once on our own machines: a game not previously listed, its report produced by
the effect, its submission checked against the rule below, its entry shipped in
a package, and a fresh session recognizing it. A release may claim that
applications can be submitted once that holds, even while no outside submission
has arrived — what is claimed is a route, not a catalogue size.

**Full acceptance.** A submission from someone outside the project accepted and
shipped, and one stale entry corrected through the same route. Both depend on
people we do not control, so this gate is recorded as outstanding rather than
waited on, and it does not block the slices that only need the route.

### Scope and boundaries

Own what a submission contains, the report the effect produces to fill it, the
privacy rule for that report, the submission form, the maintainer rule for
accepting and refusing, the provenance fields an entry carries, the human
documentation of the process, and the handling of stale and withdrawn entries.

Do not own the storage, matching or editor: they are implemented earlier in
this same document, and this topic uses them. Do not own the methods themselves
([resolution control](slice-resolution-control.md)) or the automated
**Find best method** trial, which the handbook places outside scope under
[application launch configuration](../upscaling.md#application-launch-configuration-and-method-discovery);
this process must work without that automation and must accept the same
evidence if it ever arrives. The diagnostic surfaces the report is assembled from belong
to [what the effect says](slice-development-infrastructure.md); what is
added here is the report's content, not a second developer view. Form
infrastructure, labels and release-note categories belong to the
[GitHub project workflow](slice-github-project-workflow.md); this package states
what the application form asks for.

Explicitly excluded, and not to be reintroduced as a convenience:

- No telemetry, no automatic upload, no background submission. The machine never
  reports what the user plays. The only thing that leaves it is text a person
  read and chose to paste.
- No list fetched at runtime and no online catalogue. The list stays a file in
  the package, reviewed before it ships, because a list downloaded into a
  compositor effect is a supply chain nobody signed up for.
- No account, no server, no service of ours.
- No personal data in the installed file. It is system configuration and reaches
  every user of the package.

### Dependencies

The editor, the layered storage and the matching are implemented and are used
as they are. The report needs the snapshot the effect already assembles in
`observation.cpp`, plus the program name, which the effect can read and does not
currently keep for reporting. The form needs the issue-form conventions of the
GitHub slice. Release notes naming added and changed entries need that slice's
category configuration.

### What a person has to find out

Four answers make an entry. The process is written around them, and the report
exists so that three of the four come from the machine rather than from memory.

| Answer | Where it comes from | State |
| --- | --- | --- |
| Which fields identify the window | KWin's own picker, through **Add from window** | Implemented |
| Which program the connection belongs to | The effect, from the client connection; unavailable for X11 clients | To build |
| Which method the program follows | A trial with a defined success condition | Rule to write, automation later |
| What to recommend as a preset | Frame times from the effect's own statistics | Implemented; the rule for using them is to write |

Identity is read, never typed from memory. Extreme Tux Racer is the standing
example: its window class carries its version number, so an entry matching the
class would stop matching at the next package, and only the instance is stable.

A trial succeeds when three things hold together: the supplied buffer got
smaller, the image still covers the screen, and the pointer still lands where it
looks. A smaller buffer alone is not success — a window that no longer fills the
output is a worse result than doing nothing — and the report states all three.
A method that was not observed working is not proposed. Refusals are worth
recording too: an entry whose method is `None` recognizes the game, asks it for
nothing, and tells the next person that the question was already asked.

A preset is a measurement or it is `Automatic`. Frame time before and after,
taken through the effect's own statistics, is what turns "it looked smoother"
into a number somebody else can check. `Automatic` asks for nothing and is an
honest entry; a guessed preset is not.

### The report the effect produces

A copy action on the settings page, beside the applications, produces one block
of plain text holding exactly the fields of an entry and the conditions under
which they were observed. It is assembled from what the effect saw, not from
what the person typed, except for the few things only they know: the package
version of the game, how they started it, and what they observed about the image
and the pointer.

It carries the identity fields separately, as an entry states them, the program
as a file name with an explicit statement when it could not be determined, the
method in force and what it advertised or requested, the supplied buffer and the
destination, the output's pixel size and scale, the plugin build, the KWin
version, the graphics backend, the distribution and the GPU and driver.

It carries nothing else. No absolute path — the file name is the matched field
anyway, and the full path says where somebody keeps their games. No window
title: titles carry save-game names and player names and are useless as
identity. No environment, no user name, no home directory, no tokens. The rule
is easier to keep than to audit, so the test asserts the absences.

The same text serves both routes, so nobody has to assemble it twice.

### The route

| Route | For | Result |
| --- | --- | --- |
| Issue form | Anyone with a working game and a report | A maintainer writes the entry; the issue is the record of the measurement |
| Pull request | A contributor who can build and test | The entry with its report in the description, reviewed like any change |

The form is `.github/ISSUE_TEMPLATE/application.yml`, asking for the application
and its measured version, the pasted report, the methods tried and what each one
did, the observed frame times if any, and the three-part success statement. It
says in its own text that the report must not be edited by hand, and where to
find the copy action. A submission without a report is a request, not a
measurement, and is answered with the route rather than added to the list.

The process is written for people in `CONTRIBUTING.md`, as one short section
that names the four answers and links the form. The handbook keeps the rule and
the requirement; the slice keeps the working detail until it closes.

### What we do with one

Every accepted entry passes the same checks, whoever measured it:

| Check | Rule |
| --- | --- |
| Identity survives an update | At least one constrained field must be stable. A class carrying a version number is refused unless another field is constrained. |
| Method was observed | The report shows the supplied buffer before and after, and the image and pointer statement. An unobserved method is not shipped; `None` is available and is a real answer. |
| Version stated | `MeasuredVersion` is required, because a name never implies an identity and a later mismatch has to be traceable. |
| Preset justified | A measurement, or `Automatic`. |
| Conditions stated | Backend, KWin version, output pixel size and scale. |
| Note written for a person | One or two sentences saying what the program follows and what it refuses, in the voice the shipped file already uses. |
| No personal data | Checked before the entry is committed, not after the package ships. |

Where the program is open source, the reviewer reads how it chooses its size and
says so in the entry's note, as was done for glmark2 and vkmark. For a closed
game the observation is the only evidence and the note says that too.

#### The question Jens has to answer

Shipped entries are active on installation, decided on 2026-09-18 so that
installing the package is enough for a game the effect knows. An entry we ran
ourselves and an entry one stranger measured once carry different evidence, and
the second kind is the only kind that scales.

| Option | What it gives | What it costs |
| --- | --- | --- |
| Ship only what we ran ourselves | Every entry backed by our own measurement | The list stays at four and this process produces nothing |
| Ship accepted submissions, active, with their provenance | The effect works out of the box for games nobody here owns | A wrong entry mis-sizes a game for everyone until the next package |
| Ship them active, but only for methods contained to one connection | The advertised methods change only what one client believes; X11 resize moves a real window | Refuses the method that Extreme Tux Racer needs, for the games that need it |
| Ship them inactive until a second person confirms | Two measurements before anything acts | Needs a second owner of the same game; in practice such entries never activate |

Recommendation: ship accepted submissions active, with the provenance in the
entry, and contain the risk where it actually bites. A wrong entry must be
switchable off without editing a file, which it already is; a report that an
entry is wrong must be as easy to send as the original submission, which the
same form provides; and the release notes must name added and changed entries,
so that a user whose game suddenly behaves differently can see why.

Two smaller decisions come with it. Credit: a pull request keeps its author, an
issue-borne entry is committed by the maintainer with the issue number in the
message, and the installed file carries no names — crediting people belongs in
release notes and Git history, not in system configuration. Scale: the reader
parses the whole file on reconfiguration and matching is a linear scan off the
paint path, which is fine for the size this will plausibly reach; measure it
before the list passes a hundred entries rather than assuming either way.

### Keeping the list honest

An entry goes stale when a game changes its identity or its renderer.
SuperTuxKart already shows the second case: its OpenGL renderer follows the
advertised mode and its Vulkan renderer does not, so the same name and the same
version can mean two different answers. The effect cannot detect this — it does
not know the game's version and has no business asking — so the only route back
is a person noticing and saying so, through the same form, with a report
attached. `MeasuredVersion` is what makes their report comparable to ours.

A refuted entry is corrected or removed in a package, and the release note says
which. Removing an entry the user never touched simply stops it applying;
removing one they edited leaves their own fields in place, which is the layering
working as intended and is worth saying out loud in the note.

### Acceptance criteria

Planned checks, not observed results:

- Catalogue tests extended to the rule: every shipped entry names an
  application, a measured version, a method this build knows, a unique order and
  at least one identity field. The test fails on an entry accepted without them.
- Report tests: the produced text contains each identity field, the conditions
  and the method state, and contains no absolute path, no window title and no
  environment value. Both the case where the program is known and the case where
  it cannot be determined are covered.
- Form: renders on GitHub, required fields reject an empty submission, and its
  links resolve. Coordinated with the GitHub workflow slice, which owns forms.
- Documentation: both pre-commit stages in the maintained Trixie container.
- Rehearsal, which is the supported-scope gate: one game not currently listed,
  taken through the whole route on the development machine, ending in a fresh
  session that recognizes it from the installed package.
- Outstanding: one outside submission accepted and shipped, and one stale entry
  corrected through the route.

### Progress and remaining work

- [x] Write this plan. Nothing in it is implemented; recorded 2026-09-19.
- [ ] Obtain Jens's answer on shipping unverified submissions, credit and the
      two smaller decisions above.
- [ ] Report the program name for the selected window, including the X11 case.
- [ ] Add the copy action and its tests.
- [ ] Write the trial rule and the four answers into `CONTRIBUTING.md`.
- [ ] Add the application form with the GitHub workflow slice.
- [ ] Extend the catalogue tests to the acceptance rule.
- [ ] Walk the route once end to end and record what it produced here.
