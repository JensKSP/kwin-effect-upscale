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

The settings model itself was reworked on 2026-09-20, before implementation:
see [what a setting is, and where it lives](#what-a-setting-is-and-where-it-lives-2026-09-20).
Every preference now has one global value and an optional per-game override,
the global defaults are a profile with no identity, and the single `Method`
field becomes one slot per presentation. That section supersedes the sparse
model proposed below and is what the remaining work implements.

## Historical start state

The effect initially had global settings only. Matching, profile storage and the profile
editor were not implemented. Source investigation established reusable KWin
window detection and the sparse setting model below.

## End state

A person can make this plugin behave the way they want for one game without
learning anything about how it works. Concretely, all of the following hold:

- **Every preference has one global value and an optional per-game override.**
  A key present in a profile is that game's answer, a key absent follows the
  global, and that is the only rule there is: no sentinel values, no
  value-dependent precedence, and no setting that exists in one layer only.
- **The global defaults are a profile with no identity**, disabled by default,
  which still supplies the values other profiles inherit while disabled.
- **`Enabled` means the same thing on every profile**: this profile acts. A
  profile that is switched off takes no part in matching, so its game falls
  through to the global profile, which is off by default.
- **Each profile answers per presentation** - Wayland and X11, fullscreen,
  borderless and windowed - with `Auto`, a method its protocol can carry, or
  `Off`. **An absent slot reads as `Auto` on a game profile and as `Off` on the
  global profile.** A game profile describes a game somebody looked at, so Auto
  has something to stand on; the global profile answers for programs nobody
  measured, which are asked for nothing until a person sets a slot. The two
  results for the same absent key are deliberate, and each resolver implements
  its own: `readMethods()` in `application.cpp` for profiles,
  `upscaleGlobalMethods()` in `settings.cpp` for the global one.
- **The settings page is two switches and a set of preferences.** Methods live
  in a profile's details, and a person who does not open them never meets one.
- **The editor offers Use global on every inheritable item**, reorders
  profiles, clears a profile's overrides, and never turns a displayed
  inherited value into a stored override.
- **An existing installation survives the upgrade**: the stored `Automatic`
  preset, the `-1` threshold, `UnknownApplications` and the single `Method`
  field are each read once into the new form, and no old key is reinterpreted
  as a new one.
- The shipped catalogue carries what was measured about a program and not what
  somebody would prefer, so the global resolution reaches the games we ship.

Matching, layered storage, the editor and the catalogue are already
implemented and keep working throughout; this end state is about what a
setting *is*, not about rediscovering how a window is recognised.

## Supported scope and full acceptance

Stated per the handbook's
[two gates](../upscaling.md#supported-scope-and-full-acceptance).

**Supported scope**, which authorises a release: the model above implemented
and verified on the minimum supported KWin, with every preference resolving
through one path, the migration exercised from a configuration written by the
previous release, and the editor driven by its own test rather than the class
behind it. `Auto` is inside this scope on X11, where the window exists before
anything is asked of it and a failed resize is reversible. **`Auto` on native
Wayland is explicitly outside it** until
[resolution control](slice-resolution-control.md#a-reversible-wayland-lever-for-auto-2026-09-20)
has measured the fractional-scale lever; until then a Wayland slot set to
`Auto` makes no request, and the settings page and the release note both say
so. A profile that names a measured method keeps working either way, which is
what makes that exclusion shippable rather than a hole.

**Full acceptance**, which authorises calling the requirement met: the above,
plus `Auto` working on native Wayland, plus real-session acceptance on the
television - per-game overrides surviving a restart, a game left alone by a
disabled profile, the global resolution reaching a shipped game, and the six
slots exercised across real applications on both window backends.

## Scope and boundaries

Own what a setting is and how one is resolved, identity selection, matching
priority, per-game overrides, storage, the configuration editor and the shipped
catalogue. Share one resolved settings value with every consumer; do not give
each consumer a different matcher or a different idea of what the setting says.

Own `Auto` as a stored value and as the X11 mechanism. Do **not** own what a
method can achieve: whether `Auto` can work on native Wayland, and the lever it
would use, belong to [resolution control](slice-resolution-control.md), and
this package ships the six slots whichever way that measurement goes.

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

The identity, storage, matching and editor described below are implemented. The
remaining work is the settings model of 2026-09-20, in an order where each step
leaves the plugin working:

1. **The item table and one resolution path.** Declare every setting once with
   its kind, scope and split, and resolve a complete value for a window from
   the global profile plus the profile that claimed it. Replace the static
   `UpscaleConfig::` reads scattered through the plugin, `effectiveResolution
   Preset()` and the `-1` threshold sentinel with that one path.
2. **The migration**, written at the same time as the keys it reads, so no
   release ever reinterprets an old value as a new one.
3. **The six method slots**, with the existing single `Method` translated into
   the presentation it was measured under.
4. **The editor**, built from the table rather than hand-written per item -
   which it has to be, because `upscale_config.cpp` is already over the
   file-size limit at 419 code lines and thirteen inheritable items plus six
   slots cannot be added to it by hand.
5. **The catalogue**, dropping the shipped resolution that pins SuperTuxKart
   and keeping the benchmarks' explicit `Native`.
6. **The handbook**, which records the model as implemented rather than planned.

Validate application templates from observed windows.
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
- Start without substring or regular-expression matching - superseded on
  2026-09-21 by [matching by path and by pattern](#matching-by-path-and-by-pattern-2026-09-21). Titles change during
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

Superseded on 2026-09-20 by
[what a setting is, and where it lives](#what-a-setting-is-and-where-it-lives-2026-09-20),
which keeps the `std::optional` representation and the presence rules below and
replaces the list of settings the layer covers, the treatment of `Enabled` and
the single method field. Retained because the KConfig findings under it were
measured rather than assumed.

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

The settings model of 2026-09-20 in full: the item table, per-game overrides for
every preference, the six method slots, the editor's **Use global** controls and
the migration off `Automatic`, the `-1` sentinel and the single method field.
Profile ordering in the interface and the notes' translation, which needs the
localized-entry extraction KDE uses for `.desktop` files, remain part of this
slice as well.

One question in that section is open rather than decided: what an upgrade does
with a stored global `Enabled=false`, which has no successor switch. Auto's own
Wayland mechanism is no longer in doubt as a design but is unmeasured as
behaviour; it and its bench belong to
[resolution control](slice-resolution-control.md#a-reversible-wayland-lever-for-auto-2026-09-20).

How the shipped list grows beyond the applications one machine can run is
tracked separately below, under
[submitted applications](#submitted-applications-and-the-list-we-maintain):
producing a submittable report, the route people send it by and the rule for
accepting one. That topic builds on the storage, matching and editor implemented
here and must not hold this part of the package open.

## What a setting is, and where it lives, 2026-09-20

Jens asked why the settings are split the way they are, and said that all of
them can be defined as a global default and per game. The split had no rule
behind it. Seventeen entries were global because they were written first, and
fourteen fields were per application because the catalogue needed them. Exactly
two settings existed in both layers, and each used a different idiom, neither of
which was an override:

| Where | What it did |
| --- | --- |
| `minimumPixels` | `-1` meant inherit and `0` meant no threshold: a sentinel inside the value space |
| `preset` | `effectiveResolutionPreset()` was a negotiation rather than an override. A global `Automatic` deferred to the profile, a profile `Native` overruled an explicit global, and otherwise the global won. Which layer won depended on both values |
| `Preset=Custom` in a profile | read the *global* percentage, because the percentage existed only globally. A per-game custom percentage could not be expressed at all |
| `Enabled` | globally "upscale at all", in a profile "take part in matching": one word for two concepts, and the per-game form of the global one did not exist |
| `Method` | one value per profile, although each method acts on one protocol only. SuperTuxKart's `AdvertisedMode` is a measurement of that game under native Wayland; the same game through Xwayland silently asks for nothing |

### The rule

A setting is one of two things, and that decides everything else about it.

**Something that describes the game.** Its identity, and the facts measured
about it: which request it follows in each of the ways it can run, and whether
its own mode selection refuses secondary outputs. None of this can be inherited,
because a measurement of one program says nothing about another. Whether a
profile acts at all belongs here too: that is the profile's own participation,
not a value anything else can supply.

**Something that describes what the user wants.** The resolution, the
sharpening, the output threshold, whether the effect may ask at all, and every
on-screen display choice. Each has one global value and an optional per-game
override. A key present in a profile is that game's answer; a key absent follows
the global. No sentinels, no negotiation, no exceptions.

The global defaults are therefore a profile with no identity, and **global-only
turned out to be empty**. Its one candidate, `UnknownApplications`, dissolved as
soon as that was written down: it is the global profile's method. Should a
setting ever be found that genuinely cannot be said per game, it becomes one
more row in the table below rather than a special case in the code that reads it.

### The table

Each item declares three things about itself, and the editor, the storage and
the resolver read them from there. Adding a setting is one row. Discovering
later that an item needs a different shape is an edit to that row plus a note
about what was already stored, not a sweep through the plugin. That is what
makes developing as we go affordable; the state this section replaces is what
happens when each item's shape is written into every place that reads it.

| Axis | Values | Decides |
| --- | --- | --- |
| Kind | identity, fact, participation, preference | whether it can be inherited at all |
| Scope | profile only, global only, both | where it can be stored |
| Split | one value, one per presentation | whether it has the six slots below |

| Item | Kind | Scope | Split |
| --- | --- | --- | --- |
| Window class, instance, program | identity | profile only | one |
| Name, note, measured version, order | identity | profile only | one |
| Enabled | participation | every profile, never inherited | one |
| Method | fact | profile only | six presentations |
| Primary output only | fact | profile only | the X11 presentations |
| Resolution: preset and percentage | preference | both | one |
| Minimum output pixels | preference | both | one |
| Sharpening: switch and strength | preference | both | one |
| Resolution control | preference | both | one |
| Display: four switches, timeout, three corners | preference | both | one |
| Scaler and processing mode, once they exist | preference | both | one |

Two items are a pair of keys that travel together. A preset keeps its exact
ratio rather than the rounded percentage label, so Quality is 1/1.5 and not 67%,
and the resolution cannot collapse into a single number; but the two keys must
not inherit independently either, which is the defect recorded above. Setting a
profile's resolution writes both keys, and **Use global** clears both. The
sharpening switch and its strength have the same shape, with the rule that
turning sharpening off keeps the strength for later.

### Three ways to say "do less", which stay distinct

| | Enlarge a smaller buffer the game committed by itself | Ask the game for a smaller buffer | Display |
| --- | --- | --- | --- |
| Enabled off | no | no | no |
| Resolution Native | no | no | yes |
| Resolution control off | yes | no | yes |

The third row is the case with no risk attached: a game set to 1080p in its own
menu on a 4K screen is enlarged by FSR without the effect ever saying anything
to it. Native additionally bypasses processing, with the proposed
sharpen-at-native mode as the explicit exception.

`Method` and `Resolution control` overlap, because both can express "ask for
nothing", and they must not be merged: setting a measured method to `Off` in
order to express a preference destroys the measurement.

### A disabled profile falls through

Decided by Jens on 2026-09-21, reversing the decision of the day before. A
profile that is switched off takes no part in matching, by window identity and
by program alike, so a later profile can claim the window and, failing that,
the global profile does.

It still reads as leaving the game alone, and that is the argument that
decided it: the global profile is off by default, so a window that falls all
the way through is acted on by nothing. Someone who has deliberately switched
unlisted applications on has asked for exactly the windows no profile claims,
and a game whose profile they switched off is then one of them. Falling through
also keeps the one way a person could already shadow an entry this package
ships: switch the shipped one off, and their own entry claims the window.

A window left alone this way is refused with its own reason, "the application
is not in the list, and unlisted applications are switched off", rather than
the bare "disabled" it used to get. That line is what tells being left alone
apart from being broken, and it names both halves of the rule so that the
person reading it knows there are two ways to change the answer.

### The global profile is off by default

Decided by Jens on 2026-09-20, for safety: only games that were identified, and
whose behaviour was therefore confirmed, act on installation. That is also why
the catalogue ships with the package and its entries are enabled.

Its switch does not stop it supplying defaults. Participation and inheritance
are separate: a disabled global profile still hands its resolution, sharpening,
threshold and display choices to every profile that keeps them global. The
settings page therefore has to label it **Also upscale applications that are not
in the list**, never "Enable upscaling", or the switch reads as though turning
it off should stop the shipped games working.

Two consequences, recorded rather than argued:

- It closes the harmless half as well. The global profile's methods are all
  `Off`, so nothing is said to an unmeasured program in any case;
  `Enabled=false` additionally declines to enlarge a smaller buffer such a
  program committed on its own. That case cannot break a window, and it is one
  switch away.
- When it is switched on it reaches fullscreen and borderless windows alike,
  decided by Jens on 2026-09-20 and implemented on 2026-09-21:
  `upscalePresentation()` accepts a borderless window covering one output when a
  profile describes it or when the global profile acts. Something still has to
  have asked for that path, because an undecorated window covering an output is
  also what a desktop's own surfaces can look like; with the global profile off
  by default it stays unreachable until the user asks for it. It never reaches
  windowed applications: see below.

### One method is six

Decided by Jens on 2026-09-20. A game can run in six ways, and the request that
works is a property of the way it is running, not only of the program. Each of
the methods acts on one protocol: the three advertisements act on `wl_output`,
which an Xwayland game never sees, and the resize acts on an X11 window.

These six are deliberately called presentations and not modes: `AdvertisedMode`
already means a display mode.

| Presentation | Choices | Measured so far |
| --- | --- | --- |
| Wayland fullscreen | Auto · advertised mode · advertised scale · advertised mode and scale · Off | SuperTuxKart on OpenGL, glmark2, vkmark |
| Wayland borderless | the same five | nothing |
| Wayland windowed | Auto · Off | no method exists; see below |
| X11 fullscreen | Auto · resize window · Off | Extreme Tux Racer, hl2_linux |
| X11 borderless | Auto · resize window · Off | SuperTux 0.6.3 traced; no shipped entry |
| X11 windowed | Auto · Off | no method exists; see below |

**Auto is the default, so an unmeasured presentation and `Auto` are the same
thing: an absent key.** No further state is needed to express "nobody has run
this game that way yet". `Auto` still differs from `Off`: Auto means work it out
safely, Off means the question was asked and the answer is that asking is
pointless or harmful here. That keeps the distinction the catalogue header
already draws, that an entry asking for nothing still records that the question
was asked.

**These are the only settings with no Use global.** Every other setting has one,
because every other setting is a preference; a method is a measurement of one
program and has nothing to inherit. `X11PrimaryOutputOnly` stops being a
property of the profile and becomes a property of the X11 presentations.

**On the global profile the same six slots default to `Off` rather than `Auto`.**
For a game in the catalogue somebody measured it, so Auto has ground to stand
on; for a program nobody has ever run it has none. Switching on "also upscale
applications that are not in the list" therefore enlarges the buffers such
programs commit by themselves and does not begin experimenting on them until a
slot is set to Auto deliberately. This preserves the line the previous
`UnknownApplications` setting drew.

**The editor offers only what the current screen can do**, with a reason for the
rest, as the handbook already requires. On an unscaled output the two
scale-based choices can say nothing at all: see the collapse below.

### Windowed, and why it is two slots with nothing in them

Jens asked on 2026-09-20 why windowed applications are untouched, since the
image could be enlarged to fit the window. The drawing side already works that
way - the scaler's destination is `window->frameGeometry()`, not the screen - so
nothing new is needed to enlarge into a window. What is missing is a way to
obtain a smaller buffer while the window keeps its size, and the two protocols
differ:

- **X11 windowed is plausible with machinery that exists.** The fullscreen path
  already shrinks the client, presents it at the frame size and maps pointer
  input itself. The new part is holding the frame while the client shrinks;
  under fullscreen the fullscreen state does that, and for a window it would
  have to be done deliberately.
- **Wayland windowed has no path today.** There the window is the surface:
  shrink it and the window shrinks. Keeping it visually the same size means
  presenting a surface larger than the client believes it is, breaking its own
  pointer coordinates and hit testing unless it uses a viewport. That is the
  case the handbook's
  [borderless section](../upscaling.md#borderless-windows-at-the-size-of-the-screen)
  excludes.

One constraint holds for both, and is the reason the slots are safe to add
before either has a method: **a windowed presentation is only ever acted on
through a profile somebody deliberately made, never through the global
unlisted-applications row.** Fullscreen and borderless-covering-one-output are
rare enough to be a filter in themselves. "A window" is every window on the
desktop, and Auto must never be loose on that.

### The methods stay, and Auto is a new one

Decided by Jens on 2026-09-20, after the source review below. The three
advertisements and the X11 resize stay exactly as they are, and `Auto` is not a
chooser among them. It is a method of its own with its own mechanism, and on
Wayland that mechanism is not an advertisement at all.

| Slot | What Auto does |
| --- | --- |
| Wayland fullscreen, Wayland borderless | Say nothing at bind. Once the window exists and the presentation is known, ask that one surface for a fractional scale equal to the wish, watch the following commits, and put it back where it did not work |
| X11 fullscreen, X11 borderless | Resize, verify coverage and pointer mapping, put it back where it did not work |
| Wayland windowed, X11 windowed | Nothing. Auto is never loose on ordinary windows |

**Auto never makes a blind advertisement, and never borrows one.** A mode sent at
bind cannot be taken back, and the review below shows it harms two classes of
client. An earlier draft let Auto advertise where the profile had measured
`AdvertisedMode` on its other Wayland slot; review on 2026-09-20 rejected that,
rightly. A measurement on one slot is not evidence about another: SuperTuxKart
is one program whose two renderers ask for different window kinds, so the same
game is a mode-list client in one presentation and a configure-sized one in
the next. Only an explicit method in the fullscreen slot is said at bind, and
that is a measurement of that slot.

**Auto and the measured methods reach different clients, which is why both
exist.** The fractional hint reaches GLFW, SDL 3, Godot and Wine. It does not
reach SDL 2, which never implemented the protocol, or Qt, which clamps the
value to 1.0. SDL 2 is a large share of Linux games and includes SuperTuxKart,
so a measured advertisement is the only thing that moves those - and it also
starts the game at the right size instead of changing it after the first frame.
Auto failing is therefore not a dead end but the case a catalogue entry exists
for, and Auto reports it rather than falling back to something unsafe.

### What a Wayland client actually reads, source review 2026-09-20

Read in the upstream sources of SDL 2, SDL 3, GLFW, QtWayland, Godot,
`winewayland.drv` and SuperTuxKart 1.4, and in KWin 6.3.6 and master under
`build/upstream/`. **Nothing here was run.** These are statements about code,
and each needs the bench in
[resolution control](slice-resolution-control.md#a-reversible-wayland-lever-for-auto-2026-09-20)
before it is treated as behaviour.

There are four classes of client, not three, and which one a program belongs to
decides whether a falsified mode helps, does nothing, or does damage.

| Class | Who | A falsified `wl_output.mode` |
| --- | --- | --- |
| A. Configure-sized fullscreen | What most games call borderless or windowed fullscreen: SDL 2 `FULLSCREEN_DESKTOP`, SDL 3 non-exclusive, GLFW with a monitor, Qt `showFullScreen()`, both of Godot's fullscreen modes. All ask the compositor for fullscreen and take the size from the configure | **Inert.** Neither helps nor harms |
| B. Mode-list | SDL 2 and SDL 3 exclusive fullscreen. Picks the closest entry from the display's mode list and viewports it to the configure size | **Correct.** This is the class `AdvertisedMode` was measured on |
| C. Plain window sized from "the screen" | GLFW undecorated at `glfwGetVideoMode()`, Godot borderless at `screen_get_size()`. Both read `wl_output.mode` because neither binds `xdg_output` | **Harmful, and not undoable.** Qt and SDL 3 take the screen size from `xdg_output`, which we do not falsify, so they are safe here |
| D. Mode pixels with a declared scale | Wine's `winewayland.drv`, and vkmark. Takes the display and its mode list from `wl_output.mode` but never stretches back to the configure size | **Harmful:** an undersized surface inside a fullscreen state. Needs mode plus a scale equal to the ratio, which at scale 1 only a fractional scale can express |

Two beliefs recorded earlier in this repository are corrected by that review:

- **The SuperTuxKart OpenGL against Vulkan result was not the renderer.**
  `CIrrDeviceSDL.cpp` uses `SDL_WINDOW_FULLSCREEN_DESKTOP` on the Vulkan path
  and `SDL_WINDOW_FULLSCREEN` on the OpenGL one, so the difference was class A
  against class B - a window flag, not a graphics API. This matters because the
  class **is** observable to a compositor at the first commit: a class B surface
  carries a viewport whose destination differs from its buffer size, and a class
  A surface does not. The earlier conclusion that nothing visible to us decides
  the outcome was wrong.
- **The advertisement as implemented is incoherent, and SDL 2 believes it by
  accident.** `xdg_output` still reports the output's true size while
  `wl_output.mode` reports the falsified one. SDL 2 processes only the
  *n*-th `done` it was waiting for and ignores later ones, and because
  `announce()` runs synchronously inside `OutputInterface::bound`, ours is
  always the one it processes. Had the `xdg_output` done arrived first, SDL 2
  would have derived a scale factor from the disagreement instead. SDL 3
  processes both and ends up with a mode list containing the true and the
  falsified size together. This is a defect in shipped code independent of
  everything else here, and it belongs to
  [resolution control](slice-resolution-control.md).

### Why Auto does not use the advertisements

The three advertisements are not layers over one another. Each is a different
statement, correct for one class and wrong for the others, so there is no
"apply them all" that covers everybody:

| Told | Class B, mode-list | Scale-driven | Class D, mode pixels with own scale |
| --- | --- | --- | --- |
| Mode only | correct | nothing happens | undersized surface |
| Scale only | window moves off the screen | correct | window stretches past the edges |
| Both | window moves off the screen | wrong size | correct |

That is why the unlisted-application fallback already chose the mode alone,
with the reason recorded in `application.cpp`: a scale told to a program that
reads it differently moves its window off the screen, which is not something to
do to an application nobody measured.

On the hardware this effect exists for, the choice collapses anyway. A
`wl_output` scale is a whole number and there is none below one, so
`reachableScale()` returns zero on an unscaled output and `advertisementFor()`
declines every method except the advertised mode. Everything at once already is
the advertised mode there. This is exactly the gap the fractional scale closes,
because its value is a fraction with no lower bound of one.

### Detecting the presentation

The protocol is certain in both phases. After a window appears
`isWaylandClient()` and `isX11Client()` answer it directly. Before one exists it
is certain by construction: an X11 game never binds the compositor's
`wl_output`, because it reaches the compositor through Xwayland, whose
connection carries Xwayland's own executable path and therefore cannot match a
game profile.

Fullscreen against borderless is already decided in `upscalePresentation()`.
Three caveats, each already stated in the code: fullscreen is a state and not a
size, so the presentation is read once the window has settled; a window the
effect itself made smaller keeps that state, so the presentation is never
re-derived from geometry the effect changed; and a game can change presentation
during a run, so it is a property of the moment rather than of the launch.

What cannot be known at bind is the presentation, because the client has no
window then, and no ordering or binding signature predicts it: every toolkit
binds every global it knows regardless of intent. A toolkit fingerprint is
possible from the resources a client has bound - a client with no
`zxdg_output_v1` is GLFW or Godot, a client with
`wp_fractional_scale_manager_v1` is not SDL 2 - and the class, not the
presentation, is what predicts harm. That is a refinement to consider once Auto
works, not a prerequisite.

### What Auto stores

Jens decided on 2026-09-20 that an Auto which works has nothing to write down.
Nothing is persisted and nothing is learned across sessions. Within one session
Auto may remember what it observed, which helps a game relaunched while someone
is tuning it. Nothing is ever written into the user's own profile layer on the
effect's initiative, so that what was measured and what the user chose never
become indistinguishable.

### What an existing installation loses

Every row names the file and group it is read from and the key it is read
as. `kwinrc [Effect-upscale]` is the global profile; `kwinupscalerc
[Application-<id>]` is one game profile, in the user's layer. Nothing moves
between the two files, and nothing is written into the package's layer.

| Read from | Old key and value | Read as | Replaced on disk when |
| --- | --- | --- | --- |
| `kwinrc [Effect-upscale]` | `Preset=<n>`, numbered from `Automatic=0` | `Resolution=<n - 1>` in the same group; `Automatic` reads as no `Resolution`, which is the default | the settings page applies: it writes `Resolution` and removes `Preset` |
| `kwinrc [Effect-upscale]` | `UnknownApplications=true` | `UnlistedApplications=true`, and `MethodWaylandFullScreen=AdvertisedMode` in the same group | the settings page applies |
| `kwinrc [Effect-upscale]` | `Enabled=false` | nothing acts, profiles included | never: see below |
| `kwinupscalerc [Application-<id>]` | `Preset=<name>` | `Resolution=<name>` in the same group; `Automatic` reads as no `Resolution`, which follows the global one | that profile is saved: `upscaleRetireLegacyProfileKeys()` writes `Resolution` and then removes `Preset` |
| `kwinupscalerc [Application-<id>]` | `MinimumPixels=-1` | no `MinimumPixels`, which inherits the global threshold. Read as a number it would clamp to `0`, which disables the threshold, the opposite of what it said | read as absent; left on disk, where it stays harmless |
| `kwinupscalerc [Application-<id>]` | `Method=<name>` | the one slot it can have been measured under - `MethodWaylandFullScreen` for an advertisement, `MethodX11FullScreen` for the resize - and every slot `Off` for `None` | that profile is saved: every translated slot is written before `Method` is removed |

Corrected on 2026-09-21: this table first said a global `Automatic` becomes
`Native`, which would have stopped the effect reducing the very games it ships
profiles for.

The "replaced on disk" column is the part review on 2026-09-20 was right to ask
for, because it hid a defect. The editor writes only the fields a person
changed, and an old key's value, read into the profile, is unchanged by
definition - so deleting the old key on save, as first written, dropped it: the
first save after an upgrade would have lost a profile's measured method and its
chosen resolution. Each value is now written under its current key before the
old one goes. `LegacySettingsTest` asserts the exact stored and resolved result
for every row, including that a profile saved unchanged keeps both.

**All of this is read, never written.** Implemented on 2026-09-21 in
`legacysettings.cpp`: an old key is translated each time it is read, for as
long as no new key has replaced it, so the compositor never rewrites a person's
configuration on its own. The settings page replaces the old keys on Apply,
after it has written the new ones, and until then the effect and the page read
the same translation. The file is temporary by nature and can be deleted whole
once no installation can carry the old keys.

The global master switch has no successor. With a switch on every profile and a
catalogue that ships enabled, nothing stops the effect single-handedly except
KWin's own Desktop Effects entry, so reading a stored `Enabled=false` as
anything else would switch upscaling on for someone who had turned it off. **As
implemented it is honoured**: nothing acts while it is stored, and Apply keeps
it, because no new key can say "everything off" and removing it would be the
page quietly switching upscaling back on. The consequence Jens has still to
decide is what the page shows such a person: at present nothing on it explains
why no profile acts, and that is the one open question in this section besides
the Wayland measurement.

### Storage

The global profile stays in `kwinrc`, group `Effect-upscale`, through the
generated `UpscaleConfig`, so that System Settings' Defaults button and KWin's
own reconfiguration keep working. Profiles stay in `kwinupscalerc` with the
shipped file under the user's, as measured on 2026-09-18 above. Two files for
one concept is the price of KWin's conventions, and it is worth paying.

The layering brings one wrinkle: a key in the *shipped* file is present, and
under the rule above a present key overrides the global. That is why the
catalogue carries facts and not taste. It ships the methods it measured and the
primary-output flag. It does not ship a resolution, except `Native` for glmark2
and vkmark, where not silently reducing a benchmark is a statement about what
the program is for rather than a preference. `Quality` comes off SuperTuxKart,
and the global default resolution becomes `Quality`, so that a fresh install
still enlarges a known game and a changed global reaches it.

### The editor

Every inheritable item offers **Use global**, showing the value it would follow,
or an explicit value, so a switch has three positions. Displaying an inherited
control must not turn it into a stored override when another field changes.
Clearing a profile's overrides stays separate from restoring the shipped
catalogue and from the page's own Defaults: those are three different things.
The six presentations are six controls on a profile, each offering only the
methods its protocol can carry and only those the current screen can act on.

`upscale_config.cpp` is already over the file-size limit at 419 code lines, so
the table-driven form has to shrink the page rather than grow it. An editor that
hand-writes a control, a read and a write per item cannot take thirteen
inheritable items and six method slots; one that builds its controls from the
table can.

## Matching by path and by pattern, 2026-09-21

Jens observed that an executable's file name does not tell games apart: every
native Source title runs as `hl2_linux`, which is why the shipped entry for it
cannot be made specific to one game. He asked for the full executable path
with pattern matching, and for it in every case, X11 included. Decided the same
day: the path with a match type is the first gate for every entry, the window
identity an optional second, implemented on this branch once the settings work
is green.

### Two extremes, and the default between them

Stated by Jens on 2026-09-21, and the principle the matching serves. Two
extremes must both be possible, and which one applies is the user's choice:

- **Very broad and simple**: the global profile switched on and applied to
  everything drawn, as soon as it exceeds the resolution limit.
- **Very narrow and targeted**: a rule that matches a single game precisely,
  so that the effect is applied to that game and to nothing else.

**The default is the middle ground**: the shipped catalogue, a good list of
measured games, with the global profile off. So precision is not a nicety.
A rule that matches more than it says would make the narrow extreme
impossible, which is why a pattern that matches everything is refused, an
advertisement goes to a program only when the executable path alone decides,
and the editor should show which running windows a rule matches as it is
written.

The broad extreme is more than a switch. Applying the global profile to every
window above a limit means more than one scaled window per output, per-window
scaler state, and a limit measured on the window rather than on the output. It
also means watching windows resize, without making resizing any slower: every
decision is taken when a window appears, when its class or geometry changes
and on reconfiguration, never per frame, and while a person drags a window the
effect steps aside entirely. That is rendering and eligibility rather than
matching, and is to be specified in a slice of its own.

### What a path can and cannot tell apart

| Kind of game | Its program identity | Does the full path tell games apart | What does instead |
| --- | --- | --- | --- |
| Native Wayland | the game's own binary | yes: `…/common/Left 4 Dead 2/hl2_linux` is not `…/common/Portal 2/…` | - |
| X11 through Xwayland | the connection is Xwayland's, shared by every X11 game, but the window's own PID resolves to the game's executable | yes, once the window exists, through KWin's `executablePathFromPid()` - with the PID caveat below | - |
| Proton and Wine | the Wine loader, in the same Proton directory for every game | no | the window class: Steam sets `steam_app_<id>` on each Proton game |
| Python, Java, Mono | the interpreter | no, and the script is only in the command line, which is process inspection | window identity |
| AppImage | a mount point with a random name under `/tmp/.mount_…` | only by pattern | a regular expression |
| Flatpak | a path inside the sandbox | not reliably | `ClientConnection::securityContextAppId()`, the sandbox's app ID, on the connection before any window exists - to be verified against a real Flatpak game before it is relied on |

So the path answers exactly one case, but the one where it matters most:
native Wayland, where the advertisement is made at bind and the program is the
only identity there is. KWin already resolves the full path,
`ClientConnection::executablePath()` from `executablePathFromPid()`, and the
effect has so far dropped the directory on purpose. Nothing here reads a
process and nothing here is Linux-only: the path is KWin's.

### The design: two gates

Decided by Jens on 2026-09-21, replacing a first proposal that kept the file
name and added a path beside it.

**Gate 1 is the executable path, with a match type, for every entry.** It
replaces `Program`. A file-name match is simply the pattern `.*/supertuxkart`,
so today's field becomes one case of the new one rather than a second field
beside it, and the shipped catalogue, which has to match wherever a game is
installed, states exactly that pattern. A person's own entry may state an exact
path instead, which *Add from window* fills from the running game.

**Gate 2 is the window identity, optional**: window class and instance, as
now. An entry matches a window when gate 1 matches and, where it states one,
gate 2 matches too. Profiles are tried in their order, as now, and the first
enabled match wins as a whole.

**Match types follow KWin's own Window Rules**, which give each property
`Exact`, `Substring` or `RegularExpression` (`Rules::StringMatch` in KWin's
`rules.h`): `ExecutableMatch`, `WindowClassMatch` and `InstanceMatch`, each
absent meaning `Exact`. That also removes a workaround the catalogue already
carries: Extreme Tux Racer puts its version in its window class, so its entry
matches the instance instead, where `Extreme Tux Racer .*` would say what is
meant.

**Where the path comes from**, in both protocols, is KWin and never our own
reading of a process:

- a native Wayland client: `ClientConnection::executablePath()`, resolved by
  KWin from the connection's own credentials;
- an X11 window: `executablePathFromPid()`, exported by KWin in
  `utils/executable_path.h`, given `X11Window::pid()`. That PID is the
  window's `_NET_WM_PID`, which the client sets and the X server does not
  verify, and a client inside its own process namespace - Flatpak uses one;
  Steam's runtime is to be checked - reports a number that means another
  process on the host. A PID that does not resolve therefore makes gate 1 not
  match, and never matches something by accident.

### Four consequences the implementation has to carry

1. **Wine and interpreters need gate 2 in practice.** Their executable is the
   loader or the interpreter, so gate 1 alone says only "some Wine game"; gate
   2 names the game, and for Proton the window class `steam_app_<id>` does so
   uniquely. *Add from window* states gate 2 by itself when gate 1 is a loader
   or interpreter shared by many programs.
2. **Before a window exists, only gate 1 can be checked**, and that is when a
   Wayland advertisement is made. At bind, only an entry that states no gate 2
   may advertise: one that does cannot yet be known to match, and an
   advertisement cannot be taken back.
3. **A per-window identity cache.** Eligibility asks which profile claims a
   window for every window of every frame, which is affordable while it
   compares strings. Resolving a PID to a path is a system call and a pattern
   is a regular-expression match per entry, and neither belongs in a frame. A
   window's path and the profile it matches are resolved once - when it
   appears, when its class changes, and on reconfiguration - and frames read
   the cached answer.
4. **An entry with only gate 2 stays allowed**, so that a game whose PID does
   not resolve can still be given a profile rather than none.

### What a pattern has to be held to

1. **Compiled when configuration is read**, into `QRegularExpression`, and
   never in a frame.
2. **Anchored to the whole value.** `hl2_linux` must not match
   `…/hl2_linux_old`; a pattern that wants part of a value says so.
3. **A pattern that matches everything is refused.** An entry constraining no
   identity is already dropped because it would claim the desktop's own
   windows, and `.*` is the same hazard in another form. A pattern that
   matches the empty string is refused, and the editor names the entry.
4. **An invalid pattern disables its entry and says why.** It never silently
   matches nothing, and never everything.

### Not decided yet

Whether a Proton game's `steam_app_<id>` should be offered by *Add from window*
as the natural identity, which it is, or left to the person. And whether the
Flatpak app ID is worth a field of its own once it has been seen on a real
Flatpak game.

## Acceptance criteria

Planned checks, not observed results:

- Unit tests: exact class/instance matching, aliases, case differences, missing
  identity, first-match ordering, disabled profiles and no-match fallback.
- Persistence tests: absent/false/zero, explicit values equal to globals,
  round trips, clear override, deletion, invalid values and future format
  handling. Changing globals updates only inherited fields after reload.
- Resolution tests for the 2026-09-20 model: every preference resolved from
  global plus override, a present key winning over the global including where
  the two are equal, resolution and percentage travelling together, sharpening
  and strength travelling together, a disabled profile claiming its window and
  doing nothing, and the global profile supplying defaults while disabled.
- Method slot tests: a method stored under the presentation it was measured
  under, an absent slot resolving to Auto, Auto distinguished from Off, slots
  offering only the methods their protocol carries, and the global profile's
  slots defaulting to Off.
- Migration tests: a stored `Automatic`, a `-1` threshold, an
  `UnknownApplications` entry and a single `Method` field each read once into
  the new form, with old keys never reinterpreted as new ones.
- Editor tests: create/read/update/delete, reorder, detection cancellation and
  errors, Apply/Reset, inherited controls, and dependent preset/sharpening values.
  An inherited control must not become a stored override when another field
  changes, and clearing overrides stays separate from restoring the catalogue
  and from the page's Defaults.
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
- [x] Specify the settings model: item table, inheritance, method slots, 2026-09-20.
- [x] Implement the item table and resolution, replacing the static
      `UpscaleConfig` reads, `effectiveResolutionPreset()` and the `-1` sentinel,
      2026-09-21: `settings.{h,cpp}`.
- [x] Implement the six method slots and their migration from the single field,
      2026-09-21: `presentation.h`, the reader in `application.cpp`.
- [x] Implement Auto as its own method per protocol, 2026-09-21: the resize with
      verification on X11 in `x11resolution.cpp`, the fractional scale in
      `waylandscale.{h,cpp}` and the decision that uses it in `autorequest.cpp`.
      Reported working by Jens on a real session on 2026-09-21; which game, which
      protocol and what buffer arrived are still to be recorded here. The
      [resolution-control bench](slice-resolution-control.md#a-reversible-wayland-lever-for-auto-2026-09-20)
      remains the measurement that moves Wayland Auto into the supported scope.
- [x] Implement the editor's **Use global** controls, 2026-09-21: built from the
      table by `settingcontrols.{h,cpp}` and `methodcontrols.{h,cpp}`.
- [ ] Profile reordering in the editor.
- [ ] Match by executable path and by pattern, per
      [matching by path and by pattern](#matching-by-path-and-by-pattern-2026-09-21).
- [x] Read a previous release's global keys under their old meaning, 2026-09-21:
      `legacysettings.{h,cpp}`, read-side only.
- [ ] Decide what the settings page shows a person whose stored `Enabled=false`
      is keeping every profile from acting.
- [ ] Update the handbook's settings page, per-application overrides and
      resolution-control sections to the implemented model.
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

An entry goes stale when a game changes its identity, or when the same game
asks for its window in a different way. SuperTuxKart shows the second case: its
OpenGL path follows the advertised mode and its Vulkan path does not. The source
review of 2026-09-20 found the reason to be the window flag rather than the
graphics API - `SDL_WINDOW_FULLSCREEN` against `SDL_WINDOW_FULLSCREEN_DESKTOP` -
which is why one program needs an answer per presentation rather than one
answer. It also means the same name and the same version can still mean two
different answers, because a game can change which flag it uses between
releases. The effect cannot detect that across launches — it does not know the
game's version and has no business asking — so the only route back is a person
noticing and saying so, through the same form, with a report attached.
`MeasuredVersion` is what makes their report comparable to ours.

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
