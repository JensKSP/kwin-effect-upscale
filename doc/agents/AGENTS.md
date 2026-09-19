<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Temporary slice documents for coding agents

Follow the [repository rules](../../AGENTS.md) and
[documentation rules](../AGENTS.md). This instruction file is permanent; the
slice documents beside it are temporary.

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
  slice and link the dependency. Give each task and acceptance result one
  owning slice; use links instead of copying status between documents.
- Distinguish planned checks from tests that actually ran. Record failures and
  outstanding acceptance honestly, including real-device checks where required.
  Keep working copies, binaries and check caches under the repository's ignored
  `build/` directory, not here.
- Do not create separate plans, progress logs, evidence folders, transcripts or
  TODO files for the same slice. These working documents are not permanent
  specifications and must not become the only explanation of the code.
- Preserve lasting requirements and design decisions in the permanent
  [human documentation](../upscaling.md), and explain implementation invariants
  and non-obvious decisions in source comments beside the code. Source code,
  comments and tests together with human documentation are the single source
  of truth.
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
| Development infrastructure: About, logging and diagnostic OSD | [Development infrastructure](slice-development-infrastructure.md) |
| FSR rendering, HDR/VRR and acceptance | [FSR rendering](slice-fsr1-hdr-vrr.md) |
| Application matching and setting overrides | [Application profiles](slice-application-profiles.md) |
| Obtaining original smaller game buffers | [Resolution control](slice-resolution-control.md) |
| Superseded managed-launch proposal | [Application launching](slice-application-launching.md) |
| Interactive in-game controls and comparison | [Game controls](slice-game-overlay.md) |
| Aspect ratio and integer scaling | [Scaling geometry](slice-scaling-geometry.md) |
| Shipping the effect in the user's language | [Translations](slice-translations.md) |
| Validated package and release publication | [Build and release pipeline](slice-build-release-pipeline.md) |
| GitHub contribution, security and maintenance workflow | [GitHub project workflow](slice-github-project-workflow.md) |
