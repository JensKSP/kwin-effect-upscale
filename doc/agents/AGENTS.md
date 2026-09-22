<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Temporary slice documents for coding agents

Follow the [repository rules](../../AGENTS.md) and
[documentation rules](../AGENTS.md). This instruction file is permanent; the
slice documents beside it are temporary. Read it before starting, continuing,
splitting or finishing a slice, and before creating or deleting a document
here.

Laid down by Jens, 2026-09-17; documentation layout revised 2026-09-18. This
workflow applies only to this repository. It replaces the earlier instruction
to keep plans and research in a separate private repository.

- Create one document per major slice before implementation. A slice is a
  bounded work package about one general topic, not a running collection of
  feature ideas. Use a descriptive `slice-<topic>.md` filename.
- State the starting behaviour and evidence, the observable end state, scope
  and explicit exclusions, dependencies, approach and acceptance criteria.
  The end state must be testable and achievable without adding unrelated work.
- State both gates from the handbook's
  [supported scope and full acceptance](../upscaling.md#supported-scope-and-full-acceptance):
  the supported scope this package can close against and release under, and the
  full acceptance that keeps its requirement open. A package whose only gate is
  full hardware acceptance can never close and gives no release signal.
  Mark optional future features in the human specification; do not turn them
  into required tasks for an open slice.
- Maintain progress, findings, observed test results and remaining work in
  that same document. When work crosses a topic boundary, create a separate
  slice and link the dependency, instead of extending an existing slice
  indefinitely. Give each task and acceptance result one owning slice; use
  links instead of copying status between documents.
- Distinguish planned checks from tests that actually ran. Record failures and
  outstanding acceptance honestly, including real-device checks where required.
- Do not create separate plans, progress logs, evidence folders, transcripts or
  TODO files for the same slice. These working documents are not permanent
  specifications and must not become the only explanation of the code: the
  [documentation rules](../AGENTS.md) say where lasting requirements, design
  decisions and implementation explanations belong.
- Delete a slice document only when implementation is complete and every
  required test has passed, including real-device acceptance where required.
  Keep it while work or required testing remains open. Before deletion, move
  any lasting information to its permanent home and update or remove all links.
- Finishing a slice never deletes these instructions or the permanent human
  documentation. Completed development history remains available in Git.

## Work package navigation

These links locate the current slice documents. Status, dependencies and
remaining work belong in those documents, not in this index. Update the links
when splitting, renaming or completing a slice. Implementation and remaining
acceptance are recorded in each owning document; historical sequencing does
not imply that already implemented diagnostics or resolution control are absent.

| Topic | Working document |
| --- | --- |
| About and logging, the on-screen displays passive and interactive, and the languages the texts ship in | [What the effect says](slice-development-infrastructure.md) |
| FSR rendering, aspect ratio and integer scaling, HDR/VRR and acceptance | [FSR rendering](slice-fsr1-hdr-vrr.md) |
| Application matching, setting overrides and submitted applications | [The applications we know about](slice-application-profiles.md) |
| Obtaining original smaller game buffers | [Resolution control](slice-resolution-control.md) |
| Validated package and release publication | [Build and release pipeline](slice-build-release-pipeline.md) |
| How the pipeline is composed, named and paid for | [Pipeline modules](slice-pipeline-modules.md) |
| GitHub contribution, security and maintenance workflow | [GitHub project workflow](slice-github-project-workflow.md) |

Six documents were closed on 2026-09-20. Distribution packages was deleted on
Jens's instruction with its implementation complete and verified in containers
for all three distributions on both architectures. Its full-acceptance gate,
real-device acceptance on Fedora, openSUSE and Arch, has no acceptance host and
was outstanding at deletion; it is recorded permanently in the
[README](../../README.md#packages), which names only Debian Trixie as tested on
real hardware and the rest as untested container builds, in the release notes
the publisher generates, and in the handbook's
[distributions beyond Debian](../upscaling.md#distributions-beyond-debian).

The other five were merged away. The managed-launch proposal was
deleted: nothing in it was open, and the decision that superseded it lives in
the handbook. Interactive controls, translations, scaling geometry and
application submissions were specified but never started, and each continued a
topic another document already owned, so each moved into that document with its
gates and open items intact. Merging closed nothing.
