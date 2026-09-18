<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Build and release pipeline

## Start state

Existing CI and package/release workflows are under revision. Review found
publication was not consistently gated on supported-platform checks and a
hosted run failed on Git ownership. The observations below and current work
remain unfinished; no successful hosted release gate is claimed.

## End state

The exact tested commit produces reproducible, installable packages and source
archives, and shared supported-platform checks gate their publication with
checksums and verifiable provenance. Hosted release/nightly workflows and
repository protection have been exercised as required by the criteria below.
KWin master remains a separate compatibility check, not a release gate.

## Scope and boundaries

Own build/check orchestration, packaging, release gates, signing/attestation,
publication and associated regression coverage. These form the path from a
candidate commit to verified published assets. Rendering features and game
acceptance belong to their feature slices; do not accumulate their unfinished
implementation here. Additional product features are outside this package.
The [About and notices package](slice-about-and-notices.md) owns the identity
record and component/license inventory. Packaging consumes its archive metadata
and installed notices; its settings UI is not part of this pipeline package.

## Dependencies

Use the shared dependencies and maintained container environments. Required
hardware acceptance of a release candidate is performed through the feature
slices and the handbook's hardware protocol; this slice records how that
evidence relates to published artifacts rather than redoing feature plans.
Hosted permissions, public visibility and viable BSD tooling are external
prerequisites where the acceptance criteria call for them. Keep unresolved
requirements open rather than treating missing access as a passing result.

## Approach

Make supported-platform checks a prerequisite for publishing the exact commit
being tested. Share maintained container environments between local checks and
CI, preserve the existing instrumentation work, and keep KWin master outside
the supported-platform release gate. Publish only validated deliverables, with
checksums and keyless GitHub build attestations. Public repository visibility
and required checks are part of the requested setup.

Package metadata must be deterministic. Test source archives, package
installation and removal, and repeat builds before publishing. Keep generated
files and caches under `build/`. Pin workflow dependencies and checker versions,
retain failure diagnostics, bound execution time, and reuse safe caches.

Hardware acceptance remains manual on Debian: wzpc with AMD Strix and the
workstation with an NVIDIA RTX 5090. Tests must identify the exact candidate,
KWin and driver versions, display, and observed SDR/HDR/VRR results. A software
test cannot substitute for hardware acceptance. BSD coverage needs a viable
KWin development environment; investigate before claiming portability coverage.

Resource use follows each tool's native scheduler: Ninja for developer and
archive builds, debhelper with its Ninja backend for packages, CTest's automatic
parallel level, run-clang-tidy's worker pool, gcovr's CPU mode and libFuzzer's
native workers. Preserve standard environment overrides and keep local check
groups sequential so independent worker pools do not compete for the host.
No fixed two-job caps or custom RAM/CPU scheduler.

## Acceptance criteria

- CI, tags and nightly publication share supported-platform quality gates.
- The nightly master compatibility job builds and runs its available tests.
- Package and source assets are selected explicitly and checked before upload.
- Release jobs use keyless attestations; builds have read-only repository access.
- Package versions, timestamps, checksums and provenance are reproducible and
  independently verifiable; repeat builds exercise the reproducibility claim.
- Package installation, installed metadata/loading where available, removal,
  and extracted-source builds have automated checks.
- Both maintained containers build with GCC and Clang and warnings as errors.
- Both pre-commit stages and relevant runtime/instrumentation tests pass.
- Workflow lint and regression tests cover publication and gate invariants.
- Repository protection is configured once public visibility permits it.

## Progress and observed results

- Review found the latest hosted CI failed on Git ownership before runtime tests.
- Review found publication did not depend on all supported-platform checks, and
  nightly downloaded instrumentation artifacts along with packages.
- Before implementation, 38 tooling regression tests passed in Trixie.
- Existing uncommitted instrumentation and application-profile work is retained.

### Previously observed container toolchain checks

The Wine observer exposed a missing Ninja executable in the cached Trixie
check image. CMake is already a build dependency; both maintained Containerfiles
already install GCC through `build-essential` and install Clang explicitly.
Ninja was added to `debian/control`, the shared dependency list. Both image
builds now fail if CMake, Ninja, GCC or Clang cannot run. The maintained images
were rebuilt successfully, and a C++23 program configured with Ninja, compiled,
linked and ran under both compilers in each image, with warnings as errors:

| Image | CMake | Ninja | GCC | Clang |
| --- | --- | --- | --- | --- |
| Trixie | 3.31.6 | 1.12.1 | 14.2.0 | 19.1.7 |
| neon unstable | 3.30.5 | 1.11.1 | 13.3.0 | 18.1.3 |

This verifies toolchain availability, not a build of the concurrently modified
production effect. Wine experiment packages remain confined to the disposable
research container. Rebuilding is required to update existing cached images.

- Implemented the shared environments, quality gate, package smoke checks,
  deterministic package metadata, two-build comparison, clean installation
  probe, extracted-source build, explicit release inventory, draft verification,
  keyless attestation workflow and dependency updates.
- The repository is now public, after staged and history secret scans passed.
  No signing secret is needed. Hosted attestation remains untested until merge.
- An isolated copy under `build/pipeline-check` retains the initial effect
  snapshot while unrelated effect and documentation edits continue in the main
  working tree. Results below describe that snapshot, not later source changes.
- Before the concurrency changes, GCC and Clang builds with warnings as errors
  passed in both Trixie and neon. Trixie ran five runtime cases; neon ran three.
  Coverage, ASan/UBSan, 60-second fuzzing and TSan passed. Source extraction,
  build, tests and staged installation passed. Two Trixie package builds were
  byte-identical and passed lintian; clean install, reinstall, factory loading,
  removal and purge passed.
- Native concurrency is implemented. Both hook stages and tooling regression
  tests passed again. The complete Trixie `all` run passed: GCC/Clang runtime
  checks, parallel clang-tidy, metadata, coverage (91.3% lines), ASan/UBSan,
  16 fuzz workers for 60 seconds each and TSan. Two native-parallel Debian
  package builds passed lintian and produced identical main/debug packages.
  Packaging logs confirmed Ninja used 32 jobs and all six package tests started
  concurrently, including the AppStream metadata test.
- Native GCC and Clang rebuilds and all three available runtime tests also
  passed in neon; CTest started the independent tests concurrently.
- The extracted source archive passed its native Ninja build, five parallel
  runtime tests and staged installation. Both hook stages passed after the
  packaging adjustments. Build examples now select Ninja explicitly, avoiding
  the unlimited Make job count caused by a bare CMake `--parallel` option.
- Ubuntu 26.04 package compilation failed on the frozen effect snapshot:
  `UPSCALE_NEW_API` detects `core/region.h` and incorrectly assumes that all
  newer paint, render-device and EGL signatures are available together. The
  installed KWin 6.6 headers expose only some of them. The test driver also
  assumes QRegion below 6.7. Resolving this compatibility gap is required before
  the full release matrix can pass; no check was skipped to hide it.
- Native inspection identified wzpc as Debian 13 (Trixie), PCI device
  `1002:1586`, with `amdgpu`. Display/session capabilities were not established.
  The requested inventory script was canceled and removed.
- A weekly/manual FreeBSD job is implemented, with dependencies mapped from
  `debian/control`. It has not run on GitHub; it does not gate Linux releases.
- Automatic approval review rejected setting repository protection because it
  is a consequential governance change. A concrete policy is prepared under
  `build/` and explicit approval was requested; protection remains unchanged.

## Exclusions and dependencies

Real-display rendering belongs to the rendering acceptance slice. This work
provides candidate provenance and the manual hardware procedure; it does not
claim GPU acceptance. APT repository hosting and additional distribution package
formats are outside this slice. Hosted verification depends on merging the
workflow changes; repository protection depends on the pending approval.

### KWin 6.6 package compatibility

The production rendering fix has now been committed independently. Continue
against that current source. Split region/shared-colour interfaces from the
later render-device/callback interfaces using the installed public headers.
Adapt the virtual-backend driver to both stable APIs and retain its runtime
checks on Ubuntu. Acceptance requires current-source GCC/Clang builds on
Trixie and neon, the Ubuntu package build and runtime suite, and two identical
Ubuntu packages with clean installed-plugin checks.

### Current candidate validation

- Based on the committed effect fix `cbd34c7`, separate region/shared-colour
  capabilities from render-device callbacks. Ubuntu 6.6.6 now builds with
  warnings as errors and passes all five runtime cases, including integration.
- Two Ubuntu package builds passed lintian and were byte-identical. Clean
  installation, plugin-factory loading, reinstall, removal and purge passed.
- The current source passed the complete Trixie `all` command, including both
  hook stages, GCC/Clang, clang-tidy, coverage, ASan/UBSan, fuzzing and TSan.
- Current GCC/Clang builds and all available neon runtime tests also passed.
- The nightly workflow now has a verification-only manual mode to test all
  deliverables and real keyless attestation without replacing a public release.
- Added quality-gate regressions for failed, cancelled, skipped and missing
  required jobs. Container caches now distinguish their base distribution.

## Remaining work

- Exercise the verification-only nightly candidate on GitHub, including signing.
- Run the final Debian package and extracted-source checks on the current candidate.
- Exercise hosted workflows after the changes are published.
- Record hardware acceptance in the rendering slice when displays are available.
- Exercise the prepared FreeBSD workflow and the hosted arm64 matrix.
- Apply repository protection only after the pending explicit approval.

### Hosted feedback follow-up

The first hosted candidate passed GCC, Clang, static analysis, coverage, both
sanitisers, source archive checks and Ubuntu packages on both architectures.
Debian packages and build-information regressions failed because the local Git
clone subprocess did not inherit the exact repository trust setting. Reproduced
with a foreign container UID: command-line and environment settings still fail;
a system setting for `/src/.git` succeeds. The maintained images now carry that
exact exception. Both container hook stages passed after the correction.

Agents must now watch the latest PR checks and review feedback, fix valid issues
and explain dismissed findings. Advisory CodeRabbit configuration passed schema
validation, but its repository-rule exception and owner app installation remain
pending. The verification-only nightly dispatch also exercises the prepared BSD
workflow. Hosted validation of this follow-up is still pending.
