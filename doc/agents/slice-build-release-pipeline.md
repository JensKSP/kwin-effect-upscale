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

### Hosted publication filename correction, 2026-09-19

Nightly run `35431010340` built and attested its full package matrix but failed
download verification. GitHub changed the package filename's `~` separator to
`.`; the downloaded checksum manifest still named the original files. The old
nightly remains published and the failed candidate remains a draft.

Normalize the validated release filenames before generating checksums and
attestations. Preserve package versions and the original Debian build records.
Cover the hosted rename in regression tests and validate the full candidate
through the existing nightly verification mode. Publication and physical-device
acceptance remain separate from candidate verification. Planned checks are not
yet results; no tag or release replacement is authorized by this investigation.

The correction now normalizes filenames only after the original inventory and
package metadata pass validation. Replaying it against all 17 actual artifacts
from the failed nightly produced a manifest whose checksums all passed. Payloads,
Debian versions and build-record contents remain unchanged. Regression coverage
checks public names, payload preservation, rejection before renaming and the
publication failure boundary when GitHub renames an unprepared filename.
Hosted validation of the corrected candidate remains pending. Master verification
run `35444430876` separately checks the current default branch without replacing
the public release; it does not include this unmerged publication correction.

Master verification run `35444430876` passed all jobs, including FreeBSD,
both neon compilers, all four package targets, sanitizers and the 600-second
fuzz run. Both full hook stages passed for the correction, including its new
publication regressions. Static analysis and plugin metadata validation passed.
The optional local archive check could not run its integration sessions from
the nested worktree because the Unix socket path exceeded its limit. A shorter
mount exposed a stale image lacking the declared XCB RandR development package;
the image rebuild was interrupted. The hosted source-archive check must therefore
validate the pushed revision before this PR is ready; no local archive pass is
claimed. No release or tag was changed.

The following is a chronological record of named candidates. Early failures,
pending approvals and pending hosted runs are superseded where a later dated
entry records their resolution; they are not the current PR’s status.

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
Repository protection was approved and enabled on 2026-09-18, as recorded in
the current acceptance audit below.

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
| Repository protection | After explicit owner approval, enabled and independently read back `master` protection: strict Quality gate from GitHub Actions, required PRs and resolved conversations, no force pushes/deletion and zero required human approvals. The owner's follow-up requires an escape hatch: administrators may bypass branch protection, and tag ruleset `23649284` allows only user `JensKSP` (`56653986`) to bypass its `refs/tags/v*` update/deletion restrictions. | The rolling `nightly` tag is deliberately outside the stable-tag rule. Agents must obtain explicit approval for each override, even when using owner credentials. |

## Remaining work

- The owner-review setting now requires code-owner review with zero additional
  approvals. `.github/CODEOWNERS` is on `master` and assigns `JensKSP` to all paths.
  Owner-authored auto-merge passed on PR #6 with both required statuses green.
  Verify outside-authored PR behaviour; configuration alone is not acceptance
  evidence. Agents must not merge or enable auto-merge without permission for the specific PR.
- The owner approved required CodeRabbit approval and the narrow configuration
  exception. Its request-changes workflow is configured; verify an actual bot
  approval for the current PR head in a required `CodeRabbit approval` status.
  Keep the human code-owner requirement independent. Use native PR/review events:
  an unprivileged review-event signal wakes a trusted default-branch workflow,
  which reads GitHub review metadata and publishes status without executing PR
  code or downloading its artifacts. Missing/stale approval must stay blocked.
  Local regression tests cover rejection, dismissal, subsequent pushes and API
  failures. The trusted workflow is on `master`; status publication was verified
  on PR #6 and the context is now required. The exact-head approval transition
  passed on PR #6. Live revocation remains to be observed; local regressions
  cover it.
- Finish automated review of the latest revision and process valid findings;
  keep its hosted quality gate green. The owner approved versioned CodeRabbit
  configuration and required approval on 2026-09-18; activation is recorded below.
- PRs #1 and #5 merged through owner-enabled GitHub auto-merge. Dependabot opened
  updates for all three configured locations; the owner merged those updates.
  Default-branch scheduled execution still needs observation.
- Observe the first authorized rolling-nightly publication and stable-tag
  release, including downloaded-asset and provenance verification. Do not
  create a stable version solely to test publication or count the existing
  verification-only run as a public release.
- Keep real-device acceptance with the rendering slice. Candidate provenance
  identifies what was tested; this pipeline does not claim hardware acceptance.

### Hosted feedback and review integration

- Initially, draft pull request #1 was open. Hosted package validation exposed Git's
  ownership check on the local clone's `.git` directory inside Docker. A fix
  trusting that exact source repository is prepared; hosted revalidation is
  still pending.
- Added the requirement to monitor checks and review feedback after each push,
  investigate findings, fix valid issues and explain dismissed findings.
- Prepared advisory CodeRabbit settings and validated them against its official
  schema. Automatic approval review rejected staging the configuration under
  the repository's tool-configuration ban. The owner subsequently installed the
  app and approved the exception and required approval on 2026-09-18.
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
validation; its repository-rule exception and owner app installation were still
pending at that point. The verification-only nightly dispatch also exercises the prepared BSD
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

The owner requested the CodeRabbit review badge in the README on 2026-09-18.
Added it alongside the existing CI badges and recorded that specific exception
in the repository rules; the general restriction on tool attribution remains.

### Owner review and approval gate follow-up

GitHub merged PR #1 as `461322a` through the owner's previously enabled
auto-merge. No agent initiated that merge. Dependabot subsequently opened
PRs #3 and #4 for the root workflows and environment composite action, providing
observed default-branch activation evidence for those two locations.

The follow-up branch adds the README's non-working warning and TL;DR, assigns
all paths to the owner, and explicitly forbids agents from merging or enabling
auto-merge without permission for the particular PR. The live review setting
requires code-owner review with zero additional approvals; CODEOWNERS must
reach the base branch before its enforcement can be tested.

The owner explicitly approved `.coderabbit.yaml` and required bot approval.
The configuration passed validation against CodeRabbit's official schema.
The event-driven approval verifier reads authenticated review metadata and
checks the exact head commit, including paginated review history. Its trusted
workflow runs only default-branch code. A separate unprivileged review signal
supports fork PR events without granting write access to PR code.

Both standard container hook stages passed after formatting and docstring
corrections. The local regression suite covers absent/stale approval, another
identity, requested changes, dismissal, head movement/closure, API failure,
same-commit PRs and read-only inspection. Hosted review, live status publication,
requiring the status and owner/outside-author enforcement remain to be verified
after publication and the owner's merge of the trusted workflow.

PR #5 contains this follow-up. Its first hosted CI run, `35331593827`, passed
the complete Quality gate at `44f4b1d`. GitHub reported no CODEOWNERS syntax
errors, and CodeRabbit confirmed it loaded `.coderabbit.yaml`; its review is
still pending. The owner enabled auto-merge and merged dependency PRs #2–#4.
The follow-up incorporates those changes and uses their checkout pin in the new
approval workflow. Work continues in an isolated worktree under `build/` so
concurrent C++ implementation cannot change the tree being checked or submitted.

The final PR #5 revision `9bc7760` passed CI run `35332688551` and merged through
the owner's auto-merge as `f6a3bf3` on 2026-09-18. The first default-branch
`Review approval` dispatch, run `35333197316`, succeeded. No PRs were open, so
that run establishes workflow execution but not status publication or approval.
CodeRabbit had not submitted a review on PR #5 at merge time; the bootstrap
merge is not evidence of bot approval. A documentation follow-up records the
deployed policy and provides the first open PR for status-source verification.

PR #6 triggered successful run `35333375566`, which published `CodeRabbit
approval: pending` for `300afb6`. The individual status record identified
`github-actions[bot]` (ID 41898282) as its creator; CodeRabbit itself separately
reported review in progress. Protection was then updated and read back:
`Quality gate` and `CodeRabbit approval` are both required from GitHub Actions
(app ID 15368), with strict up-to-date checking. Owner bypass, code-owner review,
stale-review dismissal, conversation resolution and force-push/deletion blocks
were retained. No agent enabled a merge or auto-merge for PR #6. Its owner-authored
PR requested no human reviewer; outside-author enforcement and the successful
bot-approval transition still await observation.

The owner requested green workflow badges and targeted checks for documentation
changes. Master Nightly verification run `35334154552` passed every job at
`f6a3bf3`, including all four package targets, both KWin-master compilers,
FreeBSD, the 600-second fuzz run and artifact signing/verification. The actual
badge SVGs then reported `CI - passing` and `Nightly - passing`. The CodeRabbit
badge reports a review count and uses the owner's requested orange colour;
it does not indicate pass/fail.

CodeRabbit review `5246743546` requested correction of the handbook's claim
that owner-authored PRs need no ownership approval. The text now states that
GitHub forbids self-approval, zero additional approvals do not prove an
ownership exemption, and normal owner auto-merge remains unverified. Where
ownership approval is required, another eligible owner or an explicitly
authorized administrator bypass is needed. No review was dismissed or overridden.

Implemented conservative file selection for PRs and master pushes. Native
pre-commit file filtering skips tooling regressions for Markdown-only changes;
whole-tree licensing, history secret scanning and repository rules remain.
The full container hook stages passed with the scope/gate regression suite.
A real documentation-only Git-tree fixture passed both native pre-commit stages:
spelling and Markdown checks ran, as did whole-tree REUSE, history secret scanning
and repository rules; tooling regression tests reported skipped. Adding a source
change to that same fixture made the docs entry point reject it before checking.
Hosted full/reduced-path validation remains open until this update is published.

### Targeted CI and approval acceptance

PR #6 at `573d3da` passed the complete hosted CI run `35336028742`, including
package and source smoke builds. CodeRabbit marked finding `4045861564`
addressed and approved that exact revision in review `5246959952`. The trusted
approval workflow `35336627559` published a successful required status.
GitHub then merged the PR as `2979b60` through auto-merge enabled by Jens.
No agent requested a merge or bypass. Protection still required code-owner
review with zero additional approvals and both status checks. This establishes
owner-authored auto-merge for the deployed settings; outside-author ownership
enforcement and live approval revocation remain separate acceptance items.

PR #7 at `3eeeb40` passed documentation-only CI run `35336895198` in 1 minute
54 seconds; the full PR #6 run took 5 minutes 9 seconds. These are individual
observations, not a timing guarantee. The hosted logs show both native hook
stages passed spelling, Markdown, licensing, secret scanning and repository
rules. Tooling regressions were skipped; the source-line limit did not apply to
Markdown. Scope outputs selected only `docs`, with instrumentation and packaging
skipped. The required Quality gate accepted exactly those intended skips.
Master CI run `35336663775` also passed the full path after PR #6 merged.
Hosted file selection is accepted; subsequent revisions still need their own
checks and review, and documentation-only master-push selection has local test
coverage but has not yet been observed on GitHub.

### Nightly package and portability failures, 2026-09-19

The nightly went red at `96e036c` and stayed red at `6bfcc78`, in three jobs.
The last green nightly was `75d5c96`. All three causes were introduced by work
that no pull request had built, which is what `6bfcc78` had already begun to
address by moving those grounds into the nightly.

FreeBSD Clang failed in 50 seconds, before compiling anything, on a bare
`KeyError` naming the `libxcb-res0-dev` build dependency. That name was
added to
`debian/control` because Ubuntu's `kwin-dev` does not pull in the package
carrying `xcb/res.h`; `tools/distribution_packages.py` is the only translation of
those names for the one platform that does not read `debian/control`, and it
had no entry. Fixed in `05ad5d0`, which also replaced the bare `KeyError` from
a set comprehension with a message naming the dependency and the file to edit,
and added `tools/test_freebsd_packages.py`. That test reads the repository's
own `debian/control`, so the next unmapped dependency now fails at push time
through the existing `tooling-tests` hook, which already selects on `debian/`.
Verified by temporarily adding an unmapped name: the guard rejected it with the
intended message.

Dispatched [BSD portability 35472700158](https://github.com/JensKSP/kwin-effect-upscale/actions/runs/35472700158)
on the branch to confirm the fix against the real FreeBSD 15.0 virtual machine
rather than reasoning about it. It passed: 169 of 169 targets built under clang
with warnings as errors, including `upscale_x11_integration_test` and
`upscale_test_driver`, and both `upscale-resolution` and `upscale-config`
passed. This is the first time FreeBSD has compiled `x11input.cpp` and
`x11resolution_present.cpp`; the previous green FreeBSD run at `75d5c96`
predates them.

The `resolute` package jobs failed on amd64 and arm64 in
`upscale-x11-integration`, not in the build. Both causes and their fixes belong
to [resolution control](slice-resolution-control.md); they are recorded there.
Ubuntu 26.04 ships KWin 6.6.6 against Trixie's 6.3.6, and the integration tests
only register below KWin 6.7, so neon skips them entirely. That is the gap the
two new tests fell through: they were validated on 6.3.6 and on neon's eleven
available entries, and never on 6.6.6.

A FreeBSD or arm64 virtual machine cannot be reproduced in a container on a
Linux host, because containers share the host kernel; the nightly uses QEMU
through `vmactions/freebsd-vm`. QEMU and KVM are present on the development
machine, so a local FreeBSD guest is possible and would move this class of
failure earlier than the nightly. Not set up; recorded as an option.

### The first FreeBSD package, 2026-09-21

The first nightly with a FreeBSD package job, at `db0009a` (#19), published
nothing: [run 35580665035](https://github.com/JensKSP/kwin-effect-upscale/actions/runs/35580665035)
failed in `Build / FreeBSD amd64 Package`, and publication waits for every
package. The build, install and staging succeeded; `pkg create` then reported
every file missing at `stage/usr/local/usr/local/...`. The packing list names
files relative to the manifest's `prefix: /usr/local`, and pkg reads each one at
root directory + prefix + entry, but `build_pkg()` passed the staged prefix as
the root. It now passes the stage. Jens noticed the missing FreeBSD package in
the release list; he chose to carry the fix in PR #20.

`tools/test_build_recipe_package.py` resolves the packing list the way pkg
does, with `cmake` and `pkg` stood in for, so this runs at push time without
FreeBSD. Observed: it passes with the fix, and against the old argument it
fails naming `etc/xdg/kwinupscalerc`. Not yet observed: a real FreeBSD
`pkg create` and the in-machine package test, which the next nightly after the
merge runs, or a manual verify-only nightly dispatch before it.
