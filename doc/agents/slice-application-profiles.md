<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: application profiles and setting overrides

## Start state

The effect has global settings only. Matching, profile storage and the profile
editor are not implemented. Source investigation established reusable KWin
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
The [overlay](slice-game-overlay.md) owns interactive controls and comparison.
[Launching](slice-application-launching.md) owns launch
definitions, restart and discovery. [Resolution control](slice-resolution-control.md)
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

## Proposed design

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

The five current settings can all be overridden: `Enabled`, `Preset`,
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

Implemented so far: `UpscaleApplication`, a code-owned catalogue with these two
entries, matching by window identity and by program, and the reports that name
a recognized application. Shipped entries are active on installation rather
than offered as templates, as decided by Jens and recorded in the
[handbook](../upscaling.md#per-application-overrides). The user-editable
profile layer, its persistence and its editor are unchanged and still to build;
the catalogue is the layer they will sit above, not a substitute for them.

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

The settings page reports how many applications are recognized and whether the
list still matches the shipped one, and offers **Restore the shipped
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

### Still open

Sparse per-setting overrides, profile ordering in the interface and the notes'
translation, which needs the localized-entry extraction KDE uses for `.desktop`
files, all remain part of this slice.

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
- [x] Implement model, persistence, editor and runtime settings resolution.
- [ ] Run acceptance tests, both compiler/container builds and TV checks.

Observed documentation validation, 2026-09-18: `pre-commit run --all-files`
passed in the Trixie container on an isolated working-tree copy under
`build/application-profiles-check`, including this new document. The first run
flagged one wording choice in codespell; it was corrected before the passing
run. No profile implementation, build or runtime test was performed for this
investigation. Retain this slice document until implementation and required
acceptance are complete.
