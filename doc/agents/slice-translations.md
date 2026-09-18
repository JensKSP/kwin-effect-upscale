<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: shipping the effect in the user's language

## Start state

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

## End state

A user who installs the package sees the effect in their own language.
English, German, French and Spanish are shipped and complete for every
user-visible string, the effects list shows a translated name and description,
and a further language is added by adding a catalogue alone. Composed strings
carry the context a translator needs. The
[handbook's language requirements](../upscaling.md#language-and-translations)
are the permanent specification.

## Supported scope and full acceptance

**Supported scope.** The four languages above, complete for the settings page,
the on-screen display and the status texts, verified in a session for each one.
This is what the package can be released with.

**Full acceptance.** Additional languages as they are contributed, review of
each translation by someone who speaks it, and the layout checks repeated on
the television for the display's longer strings.

## Scope and boundaries

Own the extraction template, the catalogue layout and installation, the
translated plugin metadata, the packaging of compiled catalogues, and the
`i18nc` context for composed strings. Own the language acceptance runs.

Do not change what the texts say: rewording belongs to the package that owns
the text. Do not add a language selector; the session's language decides.
Right-to-left layout is not required yet and must not be claimed. Machine or
agent-produced translations are a starting point for review, never a claim that
a language has been checked by someone who speaks it.

## Dependencies

The [development infrastructure](slice-development-infrastructure.md) package
owns the display and status texts this package translates; wait for a text to
settle rather than translating it twice. The
[build and release pipeline](slice-build-release-pipeline.md) owns the package
contents that the compiled catalogues become part of.

## Approach

1. Add `Messages.sh` in KDE's form and generate the template, covering the
   effect, the settings module and any string in the plugin folder.
2. Give every composed string `i18nc` context that names the frames it appears
   in, and split any string whose grammar cannot survive reordering.
3. Create `po/` with catalogues for German, French and Spanish, install them
   with `ki18n_install(po)`, and confirm the packages carry the result.
4. Add translated `Name` and `Description` entries to the plugin metadata, and
   check that the effects list shows them.
5. Run each language in a session and record what was observed.

## Acceptance criteria

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

## Progress and remaining work

- [x] Record the language requirements in the handbook, as requested on
  2026-09-18: KDE conventions, the four required languages, composed-string
  context, and the locale exception for pixel counts.
- [ ] Add the template, the catalogue layout and the installation.
- [ ] Give composed strings their context and split what cannot be reordered.
- [ ] Translate German, French and Spanish, and have each read by someone who
  speaks it.
- [ ] Translate the plugin metadata.
- [ ] Run and record the per-language session acceptance.
