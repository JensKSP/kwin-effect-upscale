<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Build and release pipeline

## Start state

At the start of this slice, publication was not consistently gated on
supported-platform checks and a hosted run failed on Git ownership. The
implementation and validation below address those findings. The current
acceptance audit and remaining work distinguish completed verification from
repository settings and publication that remain outstanding.

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
The [development infrastructure package](slice-development-infrastructure.md)
owns the identity record and component/license inventory. Packaging consumes
its archive metadata and installed notices; its settings UI is not part of this pipeline package.

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
- Package versions, timestamps and payload checksums are reproducible within
  the same build environment; repeat builds exercise that claim. Provenance
  independently verifies the artifacts and source identity. Signing bundles
  are not expected to be byte-identical between signing runs.
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
formats are outside this slice. Hosted verification can run on the PR branch;
scheduled default-branch operation depends on merging the workflow changes.
Repository protection depends on the pending approval.

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

## Current acceptance audit, 2026-09-18

Rechecked the hosted results and repository settings after consolidation. The
chronological observations below describe their named revisions; later results
supersede earlier pending statuses.

| Requirement | Observed evidence | Remaining limit |
| --- | --- | --- |
| Supported-platform quality gate | [CI run 35327328581](https://github.com/JensKSP/kwin-effect-upscale/actions/runs/35327328581) passed every job for `57e2bc7`, including both compilers, lint, tidy, coverage, sanitizers, source archive and Debian package smoke checks. Both publication callers depend on quality, version and package jobs. | Check every subsequently pushed revision. |
| Full package matrix and reproducibility | [Verification-only nightly 35323664547](https://github.com/JensKSP/kwin-effect-upscale/actions/runs/35323664547) passed Debian and Ubuntu packages on amd64 and arm64 for `28767b0`, including repeat-build comparisons and clean installation checks. | This records the tested revision and environment, not immutable dependencies across dates. |
| Moving KWin and BSD compatibility | The same rehearsal passed both neon compilers and FreeBSD Clang with their available tests. | These compatibility results do not establish real-display acceptance. |
| Keyless signing and source identity | The rehearsal's publication job attested the deliverables and verified the checksum manifest's provenance against the source commit and signer workflow. It retained a verified candidate. | The rehearsal intentionally skipped public release creation and promotion. |
| Publication inventory and failure handling | Container regressions and hosted CI passed for inventory validation, failed quality jobs and promotion recovery. The publisher downloads and compares every draft asset before promotion. | Live stable-tag publication and rolling-nightly replacement have not been exercised by the verification-only rehearsal. |
| Native tool scheduling | The observed container/package runs above exercised Ninja, parallel CTest, clang-tidy, coverage and fuzz workers; the maintained commands retain native scheduler overrides. | No custom memory scheduler or fixed worker cap is required. |
| Automated review | CodeRabbit is connected and completed reviews through `451660a`. Accepted findings are fixed, including the checksum wording in `ad1ebb2`; the image-pinning assessment is recorded below. | Review of `57e2bc7` remains pending. |
| Repository protection | GitHub reports `master` unprotected and no repository rulesets. | Apply and verify the prepared policy only after the pending explicit approval. |

## Remaining work

- Finish automated review of the latest revision and process valid findings;
  keep its hosted quality gate green. Versioned CodeRabbit configuration is
  optional while the connected app uses defaults.
- Apply and read back the prepared branch and stable-tag protection after the
  pending explicit approval. The policy requires the Quality gate and resolved
  conversations, prevents force pushes/deletion, and does not require a second
  human reviewer for the sole maintainer.
- Merge through the reviewed PR when authorized, then verify default-branch
  scheduling and dependency-update activation. Branch publication alone does
  not activate scheduled workflows on the default branch.
- Observe the first authorized rolling-nightly publication and stable-tag
  release, including downloaded-asset and provenance verification. Do not
  create a stable version solely to test publication or count the existing
  verification-only run as a public release.
- Keep real-device acceptance with the rendering slice. Candidate provenance
  identifies what was tested; this pipeline does not claim hardware acceptance.

### Hosted feedback and review integration

- Draft pull request #1 is open. Hosted package validation exposed Git's
  ownership check on the local clone's `.git` directory inside Docker. A fix
  trusting that exact source repository is prepared; hosted revalidation is
  still pending.
- Added the requirement to monitor checks and review feedback after each push,
  investigate findings, fix valid issues and explain dismissed findings.
- Prepared advisory CodeRabbit settings and validated them against its official
  schema. Automatic approval review rejected staging the configuration under
  the repository's tool-configuration ban; a specific exception is awaiting
  approval. GitHub App installation by the repository owner remains required.
- Current Debian package builds were byte-identical and passed lintian. The
  extracted source archive built, passed all five runtime tests and installed
  into a staging directory.

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

The owner installed CodeRabbit and connected the account. On 2026-09-18 its
GitHub status became pending and its comment confirmed an active review of
PR #1 through commit `28767b0`, using default settings. No review was complete at
that observation. Versioned settings remain pending the requested exception.
The current Debian clean-container installation, plugin loading, reinstallation,
removal and purge check also passed.

Hosted CI for `28767b0` passed every job, including the final Quality gate.
The verification-only nightly passed the FreeBSD 15 Clang build with warnings
as errors and both resolution/configuration tests. Both arm64 package jobs also
passed reproducibility and installation checks. The full rehearsal, attestation
and CodeRabbit review were still running at this observation.

### First review findings

The first CodeRabbit review completed with three inline findings and two
additional suggestions. Accepted the missing pre-push instructions, missing
Ninja dependency, missing final-promotion recovery and omitted Dependabot
composite-action directories. Corrections and promotion-failure regressions
are prepared; validation is pending. Promotion addresses the permanent release
ID and retries three times, retaining an explicit recovery command on failure.

Did not adopt immutable base-image digests: this pipeline intentionally tracks
maintained distribution images and refreshes apt dependencies daily. It promises
byte-identical repeated builds within the same environment, not an immutable
dependency lock across dates. Build records identify the packages actually used.
Digest pinning would require a separate image-update policy; the suggestion's
condition of immutable supply-chain inputs is not a project requirement.

The review summary also reports its default 80% docstring-coverage threshold.
That threshold is not a project check or an acceptance requirement. Keep useful
API documentation and explanations of invariants, but do not add repetitive
docstrings merely to satisfy the review service's default percentage. The
repository's configured checks remain authoritative.

The complete verification-only nightly for `28767b0` passed on GitHub (run
35323664547): supported checks, four package targets, extracted source, both
neon compilers, BSD, keyless attestation and provenance verification. It retained
a verified candidate without publishing a release. Both container hook stages
and the publication recovery regressions passed for the first review fixes.
Hosted checks and incremental review of those fixes remain pending.

### Consolidation and second review

Hosted CI for `451660a` passed every job. Its incremental review found one
inaccurate handbook statement: the checksum manifest covers the listed release
artifacts, not the separately attached provenance bundle. Corrected that
description against the publication workflow.

Consolidated the unpublished documentation commit, current slice plans and
local documentation updates onto the existing PR branch, preserving both
histories. Both container hook stages passed on the combined tree before the
checksum wording correction. Latest-revision hosted checks and review remain
required after pushing the consolidation.
