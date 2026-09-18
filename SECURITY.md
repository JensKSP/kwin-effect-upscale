<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Security policy

## Supported versions

This is an experimental, non-working development project, not a supported
production release. Security fixes target the current `master` development
branch. Older snapshots and nightly artifacts have no separate maintenance
branch or backport commitment. Identify the exact affected commit or complete
package version when reporting a problem, including local modifications.

The effect runs inside KWin and shares the desktop session's process and
privileges. A successful build, test or signature does not establish that a
candidate is safe for daily use. See the [current project state](README.md#state).

## Reporting a vulnerability

Use GitHub's [private vulnerability reporting form](https://github.com/JensKSP/kwin-effect-upscale/security/advisories/new)
for suspected security issues. You can also find it under the repository's
Security tab, Advisories, **Report a vulnerability**. Do not put an undisclosed
vulnerability or exploit details in a public issue or pull request.

Include the affected revision, distribution/KWin versions, prerequisites,
reproduction steps, expected impact and any proposed mitigation. Share only the
minimum evidence needed; redact credentials and unrelated personal information.
If the problem concerns an exposed credential, its owner should revoke or
rotate it without waiting for a code fix. Do not send an active secret as proof.

The maintainer will assess the report and coordinate fixes and disclosure
through the private advisory. There is no guaranteed response or resolution
deadline. Ordinary rendering, build and installation defects belong in public
issues when they contain no security-sensitive details.

## Dependency and repository checks

GitHub secret scanning and push protection supplement the repository's Gitleaks
checks. Supported dependency alerts and Dependabot security-update PRs supplement
the reviewed version-update PRs. They do not automatically merge changes.

These services cover only inputs and patterns they recognize. In particular,
GitHub's dependency checks do not establish the security of distribution
packages from `debian/control`, a locally installed KWin, or every vendored
shader. Distribution updates, upstream advisories and review remain necessary.
See the [build and release pipeline](doc/upscaling.md#build-and-release-pipeline)
for validation and artifact provenance.
