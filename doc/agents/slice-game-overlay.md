<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: interactive in-game controls

## Start state

At the start of implementation, the
[development infrastructure slice](slice-development-infrastructure.md) has
provided the shared passive OSD, timed detection/basic summaries, statistics,
developer information and diagnostic state. Interactive controls, comparison
and managed-restart integration remain specified but not implemented.

## End state

The existing overlay offers an on-demand settings panel that distinguishes
live and pending changes, invokes explicit managed restart, and permits a
temporary visual comparison without altering saved settings or supplied input.
Required automated and real-game/TV acceptance has passed.

## Scope and boundaries

Own interactive settings actions, input/focus handling, comparison and the
presentation of pending settings/restart actions. Reuse the diagnostic surface
and state from development infrastructure and the profile/launch contracts.
Do not reimplement passive statistics, build defaults, detection or logging.
Future filters, geometry modes and display-specific overrides do not expand
this package's completion gate. Optional full About access may reuse the
shared identity/notices, but it is not required to close this package.

## Dependencies

Development infrastructure comes first. [Profiles](slice-application-profiles.md)
provide matching and resolved settings. [Launching](slice-application-launching.md)
provides pending/effective state and restart actions;
[resolution control](slice-resolution-control.md) supplies live capabilities
and actual results. UI checks may use controlled states, but closing this slice
requires integrated live/restart behaviour and real-game input acceptance.

## Approach

1. Extend the existing overlay with an interactive mode, sharing configuration
   and diagnostic state with the settings module and passive views.
2. Apply verified live changes and show pending ones without automatic restarts.
   Keep global edits, explicit profile overrides and Use global distinct.
3. Integrate explicit managed restart using the launch package's action and
   outcome states. Rendering callbacks must not launch or wait for processes.
4. Add temporary comparison at unchanged input/destination geometry; verify
   input, focus and pointer restoration, cleanup and HDR/VRR interactions.

The permanent handbook defines [in-game controls](../upscaling.md#in-game-controls-and-applying-settings)
and [restart behaviour](../upscaling.md#restarting-a-game-with-pending-settings).
The launch owner preserves command, arguments, environment, directory and
runtime; a window match cannot reconstruct an unmanaged launch. Saving settings
never restarts a running game. These are requirements, not observed behaviour.

## Acceptance criteria

Planned checks, not observed results:

- Apply live changes without restarting; cover mixed live/pending changes,
  reverting pending values, profile inheritance and explicit save scope.
  Ordinary Apply never restarts a game. Disabled/ineligible games retain
  controls; closing the panel restores focus and pointer state.
- Interactive edits update the existing diagnostic snapshot and timed basic
  summary consistently. Detection/statistics/developer visibility and configured
  preferences survive opening and closing the panel. Passive content remains
  outside capture and takes no input after the panel closes.
- Comparison preserves saved settings and supplied resolution while switching
  paths; if split view is provided, both halves use the same source frame.
- Invoke restart only on the user's explicit choice; display close refusal,
  failure, cancellation and retry accurately. Process/launch preservation
  tests belong to the launching package.
- Run repository checks and relevant rendering/configuration/integration tests
  with GCC and Clang on both container targets. On wzpc, verify legibility,
  live/restart settings, game input and HDR/VRR during and after interaction.

## Progress and remaining work

- [x] Specify live controls, pending restart settings and comparison.
- [x] Assign the passive OSD, metrics and developer defaults to the preceding
  development infrastructure slice so this package has one completion gate.
- [ ] Integrate interactive controls with profiles and diagnostic state.
- [ ] Implement comparison and launch action integration.
- [ ] Complete required checks and real-session acceptance.

Overlay/restart documentation validation, 2026-09-18: `pre-commit run --all-files`
and the complete pre-push stage passed in Trixie on an isolated copy under
`build/overlay-geometry-check`, containing the committed source and updated
documents. Local documentation links and heading anchors resolved. No overlay
or restart implementation or runtime acceptance was performed.
This recorded documentation result predates the split into work packages.
Passive OSD requirements now belong to the preceding infrastructure slice.
