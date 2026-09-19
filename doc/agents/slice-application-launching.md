<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: managed game launching

## Superseded scope, 2026-09-19

The earlier proposal covered launch definitions, wrappers, launcher adapters,
method-discovery trials and managed restart. None was implemented. The owner’s
later requirement to operate entirely from the plugin on normally started games
supersedes that proposal; it is not an outstanding implementation package.

The [handbook](../upscaling.md#four-requirements-that-bound-every-route) records
the controlling requirements. The profile Program field identifies a Wayland
client; it is not a launch command. Changes requiring fresh output enumeration
apply on the next normal launch, without the effect restarting the application.

The original proposal and its documentation-only validation remain in Git
history. No launch, restart or discovery runtime acceptance is claimed. Keep
this notice while dependent slice documents are reconciled with that decision.
