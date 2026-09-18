<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Documentation rules for agents

Follow the [repository rules](../AGENTS.md). This instruction file is permanent.

- Documents in `doc/`, outside `doc/agents/`, are permanent and target human
  readers. Describe the program, requirements, supported behaviour and design
  rationale in clear prose. Keep them current as implementation evolves.
- The [developer handbook](upscaling.md) is the permanent requirements and
  specification. Distinguish planned capabilities from implemented behaviour.
  Keep it understandable without temporary working notes.
- Source code, including comments and tests, and permanent human-readable
  documentation are the single source of truth. Resolve inconsistencies in
  those sources; do not leave a correction only in a working document.
- Put slice plans, investigations, progress, observed test results and TODOs in
  one temporary document per bounded work package under `doc/agents/`. Each
  package has one general topic and explicit start and end states. Follow that
  directory's [instructions](agents/AGENTS.md). Do not put those working records
  among the permanent documents or create duplicate plans elsewhere.
- Before deleting a completed slice document, preserve lasting requirements and
  design conclusions here and implementation explanations beside the code.
  Remove or update its links. Human documentation and these instruction files
  remain after slices are complete.
