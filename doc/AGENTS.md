<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Documentation rules for agents

Follow the [repository rules](../AGENTS.md). This instruction file is permanent.
Read it before editing anything under `doc/` and when deciding where an
explanation belongs.

- Documents in `doc/`, outside `doc/agents/`, are permanent and target human
  readers. They describe the program, requirements, supported behaviour and
  design rationale, and the development process that the repository rules
  link to, in clear prose. Keep them current as the implementation evolves;
  they never hold an agent's working history.
- The [developer handbook](upscaling.md) is the permanent requirements and
  specification. Distinguish planned capabilities from implemented behaviour.
  Keep it understandable without temporary working notes. It is not a slice
  document and is not deleted when a slice finishes.
- **Source code, including comments and tests, and permanent human-readable
  documentation are the single source of truth.** Code, comments and tests
  specify implemented behaviour; the handbook records requirements and design.
  Keep both consistent and resolve inconsistencies in those sources; do not
  leave a correction only in a working document.
- Explain durable assumptions, invariants and non-obvious decisions in source
  comments next to the implementation. Do not leave information needed to
  understand or maintain the code only in a temporary slice document.
- Slice plans, investigations, progress, observed test results and TODOs go
  into one temporary document per bounded work package under `doc/agents/`,
  following the [slice workflow](agents/AGENTS.md). Do not put those working
  records among the permanent documents or create duplicate plans elsewhere.
  A slice document supports ongoing work and never becomes a competing
  specification; the completed slice's development history remains in Git.
- Before a completed slice document is deleted, its lasting requirements and
  design conclusions are preserved here and its implementation explanations
  beside the code. Human documentation and these instruction files remain
  after slices are complete.
