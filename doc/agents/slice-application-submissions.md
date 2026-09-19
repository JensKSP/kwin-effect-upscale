<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: submitted applications and the list we maintain

## Start state

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

## End state

A person who got a game working can produce a complete, checkable entry for it
without reading our source, send it in one place, and see it in a later package.
We have a written rule for accepting, refusing and recording such an entry, a
test that fails when an accepted entry is missing what the rule requires, and a
route back for an entry that has gone stale. The list grows from measurements
other people made, and each entry says what was measured and under what
conditions, so a later mismatch can be traced instead of guessed at.

## Supported scope and full acceptance

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

## Scope and boundaries

Own what a submission contains, the report the effect produces to fill it, the
privacy rule for that report, the submission form, the maintainer rule for
accepting and refusing, the provenance fields an entry carries, the human
documentation of the process, and the handling of stale and withdrawn entries.

Do not own the storage, matching or editor: they belong to
[application profiles](slice-application-profiles.md), and this package uses
them. Do not own the methods themselves
([resolution control](slice-resolution-control.md)) or the automated
**Find best method** trial ([launching](slice-application-launching.md)); this
process must work before that automation exists and must accept the same
evidence afterwards. The diagnostic surfaces the report is assembled from belong
to [development infrastructure](slice-development-infrastructure.md); what is
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

## Dependencies

The editor, the layered storage and the matching are implemented and are used
as they are. The report needs the snapshot the effect already assembles in
`observation.cpp`, plus the program name, which the effect can read and does not
currently keep for reporting. The form needs the issue-form conventions of the
GitHub slice. Release notes naming added and changed entries need that slice's
category configuration.

## What a person has to find out

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

## The report the effect produces

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

## The route

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

## What we do with one

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

### The question Jens has to answer

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

## Keeping the list honest

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

## Acceptance criteria

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

## Progress and remaining work

- [x] Write this plan. Nothing in it is implemented; recorded 2026-09-19.
- [ ] Obtain Jens's answer on shipping unverified submissions, credit and the
      two smaller decisions above.
- [ ] Report the program name for the selected window, including the X11 case.
- [ ] Add the copy action and its tests.
- [ ] Write the trial rule and the four answers into `CONTRIBUTING.md`.
- [ ] Add the application form with the GitHub workflow slice.
- [ ] Extend the catalogue tests to the acceptance rule.
- [ ] Walk the route once end to end and record what it produced here.
