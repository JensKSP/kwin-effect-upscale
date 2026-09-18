<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: GitHub project workflow

## Status and priority

Planning only, requested on 2026-09-18. This is a separate work package for the
public project's contribution, security and maintenance workflow. No settings,
issues, notifications or new services are enabled by writing this plan.
It does not change the implementation priority of the
[development infrastructure slice](slice-development-infrastructure.md).

## Start state

The repository is public at `JensKSP/kwin-effect-upscale`. Inspection of the
GitHub API on 2026-09-18 found:

- Issues and the repository Projects setting enabled; Discussions and Wiki
  disabled. A Projects setting does not establish that a project board exists.
- Auto-merge and automatic deletion of merged branches disabled.
- Repository secret scanning, push protection, non-provider patterns, validity
  checks and Dependabot security updates reported disabled. Dependabot alert
  enablement and CodeQL default setup have not been established separately.
- Private vulnerability reporting disabled and no repository rulesets present.
  Reinspect branch protection separately when executing the plan.
- No issue forms, PR template or release-note category configuration in the
  inspected `.github/` tree.

The [pipeline slice](slice-build-release-pipeline.md) owns the active
[pipeline PR](https://github.com/JensKSP/kwin-effect-upscale/pull/1): build and
release gates, provenance, action dependency updates, repository protection and
the initial review integration. CodeRabbit is connected and has completed its
first review. The pipeline PR contains weekly Dependabot updates for workflows
and the two composite-action directories; configuration on a review branch is
not evidence that default-branch update jobs are active. Consult that slice for
its current validation and approvals instead of duplicating those records here.

## End state

Contributors can report reproducible defects, propose changes, obtain review and
understand release contents through standard GitHub features. Maintainers have
a private vulnerability channel, useful security alerts, reviewed dependency
updates and clear check results. Release inventories and published documentation
come from the repository's authoritative sources.

Completion requires every required work item below to be implemented and its
acceptance evidence observed, plus the inherited pipeline prerequisites. The
conditional options are not completion gates. If a required feature proves
unavailable under the free public-repository offering, record the limitation
and obtain a scope decision; do not silently call that requirement complete.

## Scope and boundaries

Own contribution forms and guidance, issue organization, security reporting,
additional security analysis, dependency maintenance, merge convenience,
release-note presentation, workflow summaries, release SBOMs and documentation
publication. These support one topic: maintaining this project on GitHub.

Keep plugin behavior, KWin API work, diagnostics UI and real-device rendering
acceptance in their existing slices. Hardware forms collect evidence; they do
not implement or certify rendering. Do not change the version authority,
release-tag procedure or dependency authority in `debian/control`.

Do not duplicate the pipeline slice's protection, action-update or signing
implementation. Consume its tested interfaces and verify that the resulting
default-branch configuration is active. The infrastructure slice owns the
component/license inventory; SBOM generation consumes that inventory rather
than inventing another manually maintained component list.

## Dependencies and decisions before implementation

1. Reinspect the default branch, open pipeline PR, installed apps, repository
   settings, feature availability and current GitHub documentation. Record
   observations here with the revision or run that establishes them.
2. Coordinate with the pipeline slice before editing its workflows. It owns
   branch/tag protection and the pending approval for that governance change.
   Required status checks must name checks that actually run successfully.
3. Preserve the one-list check policy: analysis and validation commands belong
   in `.pre-commit-config.yaml` and the maintained environments. CI may arrange
   their execution and upload results, but must not introduce a hidden second
   set of checks. Investigate CodeQL CLI integration before proposing a gate.
4. Prepare concrete settings and file changes before requesting any outstanding
   account-owner approval. Existing authorization remains valid; do not request
   it again. This planning request does not approve paid services, additional
   app permissions, public test messages or changes previously rejected by
   automatic approval review.
5. Require a free public-repository path. Document limits and maintenance cost;
   do not enable metered add-ons or assume a trial establishes lasting access.
6. Inspect existing issues, labels and milestones before adding anything. Keep
   useful existing data and avoid duplicate issues or competing roadmaps.

## Required work

Implement the phases in order. Repository files and reversible checks can be
prepared together; settings that depend on successful checks follow validation.

### 1. Contribution and issue workflow

| Item | Implementation | Acceptance evidence |
| --- | --- | --- |
| Bug-report forms | Separate rendering and build/install reports. Collect plugin version/commit, distribution, KWin, architecture, GPU/driver, display settings, reproduction, expected/observed behavior and relevant logs. Explain how to redact unrelated personal data. | Forms render on GitHub; required fields reject empty submissions; each requested diagnostic has a documented way to obtain it. |
| Hardware acceptance form | Record exact candidate, AMD or NVIDIA device, driver, display, scaling mode, SDR/HDR/VRR conditions, result and regressions. Allow not-tested/not-applicable results. | A maintainer can represent the existing hardware protocol without implying that an untested capability passed. Native results remain owned by the rendering slice. |
| Feature-request form | Ask for the user's problem, expected behavior and examples; link accepted requests to their PRs. | The form distinguishes a feature request from a defect and does not require an implementation design from the reporter. |
| Labels | Keep a small vocabulary for type, component and triage state: bug, enhancement, rendering, configuration, packaging, CI, regression and hardware testing. | Labels exist once, form defaults reference real labels, and the labels support useful issue filters and release categories. |
| Release milestones | Group agreed work into version milestones; identify blockers explicitly and avoid promising dates without agreement. | The next agreed release has a milestone with linked issues and an unambiguous blocker filter. No placeholder or duplicate work is published. |
| PR template | Request problem, resulting behavior, linked issues and observed validation; request hardware results only when relevant. | A new PR shows a short usable template, with no agent attribution or unnecessary process checklist. |
| Contributor guide | Add a concise human guide linking maintained build/check instructions, KWin conventions and the review process. | A contributor can find setup, both hook stages, submission conventions and expected validation without reading agent instructions. Commands agree with CI. |

Issues track public requests and defects. Source, comments, tests and the
developer handbook remain authoritative for behavior and design. Temporary
slice records remain under `doc/agents/`; issue bodies must not become a second
copy of those working records.

### 2. Security and review controls

| Item | Implementation | Acceptance evidence |
| --- | --- | --- |
| Secret scanning and push protection | Enable the repository features supported by its free public offering, retaining Gitleaks. Decide whether additional available pattern checks are useful after inspecting their scope. | GitHub settings/API report the intended enabled state and alert access is verified. Never publish a real credential to test detection; use documented inert validation if available. |
| Private vulnerability reporting | Enable private reporting and add `SECURITY.md` with supported versions and the reporting route. Use repository advisories for coordinated disclosure when needed. | The Security tab offers the private reporting route, API state agrees, and the support policy contains no invented response-time promise. No fabricated vulnerability report is posted. |
| Dependabot alerts and security updates | Check and enable alerts plus remediation PRs for supported dependency ecosystems. | Settings/API and an available dependency scan establish the enabled state. Document which inputs GitHub recognizes and which distribution packages it does not cover. |
| CodeQL | Analyze production C++, Python tools and GitHub Actions. Use the maintained CMake build for C++; start with a scheduled scan and tune valid findings before considering PR enforcement. | A successful scan produces the intended databases/results; sampled production files and generated build inputs are represented; local execution is documented and runnable; findings are assessed rather than suppressed wholesale. |
| Dependency review | Review newly introduced supported dependencies for relevant vulnerabilities, with explicit severity/exception policy. | A representative supported-manifest change is recognized; a controlled fixture exercises policy failure through the configured local check. Record that Debian/CMake dependency coverage is incomplete instead of claiming apt vulnerabilities are covered. |

The pipeline slice owns the approved CodeRabbit approval gate and its narrowly
authorized configuration, alongside the required Quality gate and owner review.
Follow the repository rule for watching each submitted revision, investigating
findings, fixing valid issues and explaining dismissed findings. Do not add a
second AI reviewer as part of this slice. Verify that the pipeline's gate is
active before relying on it; do not duplicate its implementation here.

### 3. Routine maintenance and merge convenience

| Item | Implementation | Acceptance evidence |
| --- | --- | --- |
| Action update activation | Consume the pipeline slice's weekly grouped Dependabot configuration, including both composite-action directories. | After it reaches the default branch, inspect successful update jobs covering all three configured locations. Configuration syntax alone is insufficient. |
| Hook update PRs | Schedule reviewed updates using `pre-commit autoupdate`; retain deliberately constrained versions such as clang-format 19. Use one updater per input. | A dry run proposes only intended pins; generated PR creation is exercised when authorized; the normal check suite validates updates. No automatic major formatter migration occurs. |
| Native auto-merge | Enable availability after protection is active. Maintainers opt in on individual reviewed PRs; do not automatically approve all dependency updates. | Read back the setting and verify it waits for required checks on an authorized eligible PR. Ensure the sole maintainer is not blocked by a second-human approval requirement. |
| Merged-branch cleanup | Enable automatic deletion of merged topic branches while protecting long-lived branches. | Read back the setting and observe cleanup after an authorized topic-branch merge; the default branch remains protected. |
| Failure notifications | Establish maintainer notification settings for failed scheduled runs and security alerts. Prefer native notifications first. | Confirm intended recipients/settings and observe a real notification when available. Automated issue creation or updates require explicit authorization and deduplication; do not manufacture failures or send unsolicited test messages. |

Branch/tag rulesets are a dependency owned by the pipeline slice, not a second
implementation here. Auto-merge must not weaken their checks or authorize a
merge that the maintainer has not selected.

### 4. Release information and documentation

| Item | Implementation | Acceptance evidence |
| --- | --- | --- |
| Categorized release notes | Configure `.github/release.yml` around the agreed labels. Use GitHub's generated notes through the existing tag-triggered workflow. | Preview notes between real release refs; verify categories, uncategorized changes and intended exclusions. CMake, Debian and tag version agreement remains unchanged. |
| Workflow summaries | Add concise native job summaries for platform/compiler results, coverage, reproducibility and relevant artifact links. | Inspect successful and failing run summaries. Skipped, missing or canceled checks never display as passed; detailed logs remain available. |
| Release SBOMs | Generate an SPDX or CycloneDX inventory from actual artifacts, dependency metadata and the infrastructure slice's component/license inventory. Distinguish bundled components from runtime/build dependencies; include vendored shaders. Attest the SBOM with the existing provenance mechanism. | Validate the document, inspect representative components and licenses, confirm association with the exact package/commit, and verify the SBOM attestation. Extend release-asset inventory tests so the new artifact is deliberately allowed and required. |
| Documentation site | Publish permanent human documentation through GitHub Pages, generated from the repository. Exclude slice documents, agent instructions and build artifacts; keep Wiki disabled. | Inspect the generated site locally and on Pages, verify navigation and links, and confirm excluded files are absent from the published artifact. Document the deployed revision and avoid a second editable specification. |

## Conditional options, outside required completion

The permanent handbook lists these as optional future capabilities. Retain the
following adoption triggers; do not keep this slice open merely because an
option was not selected:

| Option | Adoption trigger and boundary |
| --- | --- |
| GitHub Projects | Issues and milestones no longer give enough overview. Use one board built from existing issues/PRs, with minimal fields and native automation. |
| Discussions | User support and configuration questions need a home separate from actionable defects. Establish categories and moderation ownership first. |
| Additional code owners | The pipeline slice now owns the requested sole-owner review policy. Extend ownership only when additional maintainers actually own areas; do not imply independent review of the owner's own PRs. |
| Public build images in GHCR | Measurements show caching leaves significant repeated environment setup. Define image refresh, digest selection, retention and trust before moving consumers. |
| Manual hardware workflows | The two Debian machines and acceptance commands are ready. Explicit trusted dispatch only; never execute arbitrary public PR code on personal workstations. Actual hardware setup and tests remain in their own slice. |
| Signed commits or release tags | The maintainer selects an identity/signing method and recovery policy. This is separate from artifact signing and must not introduce a secret signing key into PR jobs. |
| Conduct policy and saved replies | Outside participation makes community expectations and recurring answers useful. Adopt maintained wording and keep replies human-controlled. |

## Explicit exclusions

- No second release/version bot. Keep the CMake version as authority and tags
  as the release trigger; use the existing publisher and native release notes.
- No repository-wide immutable releases while `nightly` is replaced under the
  same tag. Reconsider only with a separately agreed nightly publication design.
- No merge queue until concurrent contribution volume justifies it.
- No overlapping dependency bots, duplicate AI reviews or automatic approval of
  suggested code changes.
- No stale-issue auto-closure; unresolved hardware/driver bugs remain valid
  without frequent comments.
- No separate Wiki, paid service dependency, automatic public-workstation
  runner, or new requirement inside the upstreamable plugin folder.

## Validation and completion

Use the maintained containers for repository checks and run both pre-commit
stages. Changes limited to documentation/forms do not require invented compiler
tests. Exercise changed automation with representative inputs and its normal
hosted runs; add regression tests for meaningful failure boundaries. Preserve
native tool parallelism and avoid scheduling competing full-machine jobs inside
one runner merely to make the workflow look parallel.

For each required row, record the file/revision, settings readback, run URL or
observed interaction that proves its acceptance criterion. Distinguish settings
enabled from workflows exercised. Keep external prerequisites and approvals
explicit; missing permission, notification evidence or a required hosted run is
not a pass. Do not create public test issues or merge test PRs without authority.

Before deleting this slice, ensure every required row and inherited prerequisite
has passed, preserve lasting behavior and rationale in the handbook and relevant
configuration comments, and remove its index link. Optional future capabilities
remain identified as optional in the permanent documentation.

## Observed progress and remaining work

- Repository settings and the pipeline dependency were inspected on 2026-09-18.
- This plan was written; implementation has not started.
- Documentation hooks passed in the maintained Trixie container; local links
  in this plan and the slice index resolved successfully.
- Planned checks above have not been run for these proposed features.
- Next: start this separate slice when selected, revalidate the start state and
  prepare phase 1. Coordinate with the pipeline slice before any settings change.

## References

- [GitHub issue forms](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/syntax-for-issue-forms)
- [Rulesets](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets)
- [Private vulnerability reporting](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository)
- [CodeQL build modes](https://docs.github.com/en/code-security/reference/code-scanning/codeql/build-options-for-compiled-languages)
- [Dependency graph coverage](https://docs.github.com/en/code-security/reference/supply-chain-security/dependency-graph-supported-package-ecosystems)
- [Dependabot configuration](https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference)
- [Generated release notes](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)
- [Artifact and SBOM attestations](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations)
- [GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site)
- [Immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
