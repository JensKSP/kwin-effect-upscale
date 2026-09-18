<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: managed game launching

## Start state

The handbook specifies launch definitions, method discovery and controlled
restart. They are not implemented. Game detection alone does not recover a
launch command, arguments or environment. The route for integrating launcher
options remains proposed; no decision or tested adapter is implied here.

## End state

A profile can launch its intended game, discover and reuse a verified resolution
method, and explicitly restart it with pending settings while preserving the
launch definition. Actual windows are associated with the launch, failures are
actionable, and launch/restart/discovery acceptance passes for required native,
Wine, Valve Proton and launcher cases without duplicate or unrelated launches.

## Scope and boundaries

Own program/arguments/directory/environment/runtime configuration, launcher
adapters and helper orchestration, launch association, explicit restart and
bounded method discovery with cached results. These are operations over one
managed game launch. Keep configuration and process work outside paint callbacks.

Matching and sparse settings belong to [profiles](slice-application-profiles.md);
resolution mechanisms belong to [resolution control](slice-resolution-control.md).
The [overlay](slice-game-overlay.md) invokes launch actions and displays pending
state; it does not own processes. Adding stores, unrelated launchers or future
geometry features is outside this work package.

## Dependencies

Use profile identity/storage and the resolution-method capability/result
interface. Develop launch preservation and lifecycle checks with controlled
clients first; completion requires integration with verified production methods
and actual supported launchers. The overlay is a consumer, not a prerequisite
for testing launch/restart operations through the configuration module.

## Approach

1. Resolve and document the launcher integration choice, including the proposed
   [launcher-options route](../upscaling.md#launching-through-a-launchers-own-options).
   Do not silently turn a proposed alternative into the selected design.
2. Store structured launch definitions and validate commands, paths, runtime
   and argument/environment round trips before starting a game.
3. Associate the actual game and owned helpers, retain the resolved launch in
   memory, and report observed method/buffer state separately from preferences.
4. Implement explicit graceful restart, preserving launch context and changing
   only pending settings plus helper-owned connection values.
5. Implement bounded, cancellable method discovery and cache invalidation;
   ordinary launches reuse results and never run unattended trials.
6. Complete lifecycle and real-launch acceptance, and preserve the supported
   launcher contracts in source and the human specification.

## Acceptance criteria

Planned checks, not observed results:

- Restart integration: preserve exact arguments, empty values, Unicode paths,
  working directory, inherited/overridden/removed environment, runtime/prefix
  and launcher association. Cover deliberate launch edits, fresh helper
  connection values, missing unmanaged launch information, preflight failure,
  save dialogs, close refusal/timeout, cancellation, failed relaunch and retry.
  Verify no duplicate instance, unrelated-process termination or automatic
  discovery, and confirm actual buffer/method after the new window appears.
- Launch configuration: native/Wine/Valve Proton and external-launcher paths,
  additions/replacements/removals in the environment, paths with spaces and
  Unicode, exact argument boundaries and no unintended shell interpretation.
- Discovery: only applicable methods are attempted; report exact/adjusted/
  failed targets, close each trial before the next, support cancellation and
  bound waits. Cache compatible results separately from explicit user choices;
  invalidate on relevant runtime, helper, target or launch changes.
- Preserve explicit overrides and Automatic supplied-buffer behaviour. Report
  unverified input/HDR/VRR checks separately from automatic observations.
- Run repository checks and relevant tests in both supported containers with
  GCC/Clang and warnings as errors, then real-launch/restart acceptance on wzpc.
  No newly launched process counts as successful control without its actual
  window, buffer and effective method being observed.

## Findings and observed results

### Launch configuration and discovery follow-up, 2026-09-18

Jens requested a per-application launch definition and automatic investigation
of the best working resolution-control method. The handbook now specifies
program, arguments, working directory, environment changes, runtime/prefix and
external-launcher integration, plus an explicit **Find best method** workflow.
Trials must observe the real game buffer and presentation, distinguish verified
checks from user observations, close gracefully between launch-time methods,
and cache results separately from explicit user overrides. Ordinary launches
reuse compatible results without cycling through test instances.

This is specified, not implemented. Required acceptance includes exact argument
and environment round trips, inherited environment removal, paths with spaces
and Unicode, runtime/prefix preservation, external-launcher delegation and
window association, bounded failures and cancellation, no duplicate trials or
unrelated-process termination, target adjustment versus success, result-cache
invalidation, explicit-method preservation and honest unverified feature status.

Restart requirements were added on 2026-09-18 in the
[handbook](../upscaling.md#restarting-a-game-with-pending-settings). Reuse the resolved
launch including inherited environment, preserve save/exit dialogs and clean
up only owned helpers. Unmanaged games need a verified adapter or supplied
launch definition; never guess an identical relaunch from a window match.

Documentation checks for these requirements passed in the earlier isolated
Trixie copy `build/overlay-geometry-check`. That result predates this split and
does not establish implementation or runtime acceptance.

## Remaining work

- [ ] Resolve the launcher integration route and its supported scope.
- [ ] Implement launch definitions, adapters, association and lifecycle.
- [ ] Implement restart and method discovery with their acceptance coverage.
- [ ] Complete required builds, real-launch checks and durable documentation.
