<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: GitHub project workflow

## Status and priority

Implementation started on 2026-09-18 under the request to complete the proposed
improvements. This separate work package covers the public project's
contribution, security and maintenance workflow. PR #9 has merged: contribution
forms and guidance are on the default branch, with interactive form acceptance
still pending. Provider secret scanning, push protection, Dependabot alerts and
security updates, and private vulnerability reporting are enabled and read back.
CodeQL and dependency review are implemented and locally validated. Dependency
review has passed a hosted run; CodeQL's hosted execution remains pending.
Neither check gates a pull request.
Remaining product diagnostics are owned separately by the
[development infrastructure slice](slice-development-infrastructure.md).

## Historical start state

The repository is public at `JensKSP/kwin-effect-upscale`. Inspection of the
GitHub API on 2026-09-18, before implementation, found the following. These are
historical observations; current progress is recorded above and below:

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

At the start of this plan, the [pipeline slice](slice-build-release-pipeline.md)
owned the then-active [pipeline PR](https://github.com/JensKSP/kwin-effect-upscale/pull/1): build and
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

The form for submitting a recognized application is not listed above. Its
fields, its privacy rule and the rule for accepting a submission belong to
[application submissions](slice-application-submissions.md); this slice supplies
the form conventions, labels and release-note categories it uses.

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
- The initial plan was written; phase 1 implementation is recorded below.
- Documentation hooks passed in the maintained Trixie container; local links
  in this plan and the slice index resolved successfully.
- Validation not explicitly recorded below remains pending.
- Next: verify the activated contribution forms and obtain their hosted
  validation. Native security controls are enabled, as recorded below.
- CodeQL and dependency review are implemented and locally validated.
  Dependency review has passed a hosted run; CodeQL's hosted execution remains
  pending, and neither check gates a pull request. See the phase 2 record below.

### Phase 1: contribution entry points (preparation history)

Reinspection after master `2979b60` found no issues or milestones. Existing
labels already cover bugs, enhancements, documentation, build, CI, upscaling
and tests; reuse those rather than create competing names. Auto-merge, merged
branch deletion, Discussions and Wiki are now enabled. Preserve these settings;
the older start-state snapshot does not authorize undoing intervening changes.
At that inspection, secret scanning, push protection, Dependabot security
updates and private vulnerability reporting were disabled. The security phase
below subsequently enabled them. CodeQL default setup remains unconfigured.

Prepare four native issue forms (rendering, build/install, hardware acceptance,
feature request), a short PR template and a contributor guide with diagnostic
sources and maintained container commands. Forms reference only existing labels,
accept unknown/not-tested evidence honestly and require the minimum facts for
each report. Keep blank issues available for reports the forms cannot express.
Do not post synthetic issues or invent a release milestone without agreed
contents. Validate YAML and Markdown with both maintained hook stages, compare
form fields with GitHub's current schema and verify referenced files/labels.
Hosted rendering and required-field behaviour remain acceptance items after the
forms reach the default branch.

Prepared the four forms, the PR template and `CONTRIBUTING.md`; the README links
the guide. All form labels already exist in GitHub. Required fields use empty
inputs or placeholders rather than prefilled results, so untested hardware
cannot acquire a default passing result. Compared form element types, IDs,
attributes and validation keys against GitHub's form-schema documentation. Both
maintained container hook stages passed with the new files staged, including
YAML, Markdown, spelling, licensing, repository rules and tooling regressions.
Hosted CI/review and default-branch form rendering remain pending. No issues,
milestones or account settings were changed in this phase.

Hosted CI run `35338278702` passed for `c44c4f7`. CodeRabbit review
`5247163132` found that the build/install and hardware forms requested raw
package/artifact URLs, which can carry temporary access tokens. Changed those
fields to request package filenames/versions or workflow run/artifact IDs and
explicitly exclude signed URLs and tokens. Latest-revision CI and review remain
required after publishing the correction.

### Phase 2: CodeQL and dependency review

This work is based on `master` at `c03e834`. The separate security-reporting
branch, PR #10, edits the same slice document; its record of the enabled
repository settings and the resulting conflict belong to that pull request.
Secret scanning, push protection, Dependabot alerts and private vulnerability
reporting are not touched here.

Reinspected the live settings first. The repository is public, secret scanning,
push protection and Dependabot security updates report enabled, and non-provider
patterns and validity checks report disabled. CodeQL default setup reports
`not-configured`, with `actions`, `c-cpp` and `python` as its detected
languages, which are exactly the three this scan covers.

Default setup was not adopted. Its C++ path runs autobuild on a hosted runner,
which cannot configure this project against KWin and KDE Frameworks. Instead
`tools/run-codeql.py` drives the pinned CodeQL command line over the project's
own CMake and Ninja build inside the maintained Trixie container.
`upscale-codeql` in `.pre-commit-config.yaml` is its single definition,
`tools/run-checks.py codeql` runs it, and `.github/workflows/codeql.yml` runs
that same entry point weekly and on dispatch before uploading SARIF. No second
list of checks was introduced. The command line is pinned by release tag
`codeql-bundle-v2.27.0` and its published SHA-256, verified on download; `curl`
was added to the Trixie Containerfile for that fetch, since the bundle is a
release archive rather than a distribution package.

Observed results, all on the current branch content:

- The three scans completed in the maintained container through the real entry
  point. C++: 18 of 19 files; Python: 35 of 35; Actions: 13 of 13. Zero results
  in all three at the `code-scanning` suite.
- Querying the C++ database confirmed the production translation units
  `main.cpp`, `scaler.cpp`, `upscale.cpp` and `upscale_config.cpp`, and the
  generated inputs `buildinfo.cpp`, `upscaleconfig.cpp`, the moc and resource
  units and the Wayland protocol sources. The one unextracted file is
  `autotests/resolution_fuzz.cpp`, which compiles only in the Clang fuzzing
  configuration; that is recorded rather than worked around.
- A first container run swept 5973 files from the unpacked bundle into the
  Python database. Per-language path filters now confine Python to `tools/` and
  the workflows to `.github/`; a regression test covers the filter and that C++
  keeps none.
- Both container hook stages passed with the new files staged, and the tooling
  regression suite passed with 90 tests.

Dependency review is implemented as `tools/dependency_review.py` against
GitHub's dependency-graph comparison, rather than a second policy configured
only for the hosted run. `.github/workflows/dependency-review.yml` runs it on
pull requests. The policy blocks advisories from moderate upwards on introduced
dependencies, treats an unreadable severity as worse than critical, and records
exceptions by GHSA identifier with a reason; the exception list is empty.

- A representative supported-manifest change was recognised: comparing
  `461322a...f6a3bf3` reported `actions/checkout` added in
  `.github/workflows/review-approval.yml` with no advisories.
- A controlled fixture run through the same entry point blocked a high-severity
  introduced dependency, passed a low-severity one and ignored a critical
  advisory on a removed dependency, exiting non-zero.
- The output states the coverage limit on every run: the graph resolves the
  pinned actions only. `debian/control`, the CMake and KDE Frameworks
  interfaces, the pre-commit hook versions and the vendored shaders are not
  represented, and no claim is made that apt vulnerabilities are covered.

Neither check gates a pull request. The scheduled scan publishes findings so
they can be assessed first, and dependency review is not a required status.
Requiring either remains an owner decision and was not requested here.

PR #12 carries this work. Its `Introduced dependencies` job passed on the first
hosted run, `35352148918`, and recognised the dependency this very change adds:
`actions/checkout` in `.github/workflows/dependency-review.yml`, no advisories.
The coverage statement appeared in the hosted log. That is hosted acceptance of
the dependency-review row's recognition criterion; a hosted policy failure has
not been provoked, and will not be by introducing a vulnerable dependency.

The initial dispatch failed with HTTP 404 while the workflow existed only on
the review branch. PR #12 merged on 2026-09-18, so that prerequisite is resolved.
The 2026-09-19 readback still found no hosted CodeQL runs. Hosted execution,
SARIF upload and Security-tab results remain unverified; local results do not
establish them. No dispatch was performed as part of the documentation audit.

CodeRabbit review `5248718111` on `b7adc8b` raised one valid finding: the
installer reused whatever bundle was already unpacked, so raising the pin would
have left the scan on the command line a previous pin had unpacked. The
installation now records the release tag and digest beside the bundle and reuses
it only on an exact match. Observed both branches: an existing unmarked
installation was discarded and refetched, and a marked one was reused without a
download, each followed by a passing scan with no results.

A defect surfaced while pushing this branch and is fixed here rather than worked
around. Git exports `GIT_DIR` and its companions to a hook, and the four
fixture-driven regression suites inherited them, so under the real pre-push hook
their `git -C <fixture>` calls operated on the repository being pushed: fixture
files staged into its index, `tools/quality_gate.py` renamed to `doc/renamed.md`
there, `README.md` modified, and their own commits failing against the real
hooks. An ordinary checkout hides this behind a relative `GIT_DIR`; a linked
worktree under `build/`, which is how this work is done, does not. The suites now
drop those locations first, `tools/test_git_fixture.py` covers the case, all 94
tests pass with an inherited `GIT_DIR`, and this branch's own push left the index
clean.

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

## Security controls: preparation and activation

The security phase began while PR #9 was under review; that PR has since merged.
The initial readback on 2026-09-18, before activation, found Dependabot
alerts disabled (the documented 404 response), automated security fixes disabled,
private vulnerability reporting disabled and both secret scanning and push
protection disabled. Dependency-graph SBOM retrieval also returned 404; that
response alone does not establish which manifests GitHub can recognize.

Enable provider-pattern secret scanning and push protection, Dependabot alerts
and automated security updates, and private vulnerability reporting through
the documented repository APIs. These are free public-repository features.
Add `SECURITY.md` describing experimental-version support and the private route;
do not promise response times or claim the prototype is safe for daily use.
Read back each setting and query alert access without printing secret values.
Leave generic-pattern/validity options for assessment, and CodeQL/dependency
review for their local-check integration. Do not publish fabricated alerts.

Enabled and read back provider secret scanning, push protection, Dependabot
alerts/security updates and private vulnerability reporting on 2026-09-18.
Alert endpoints were accessible and returned zero alerts at inspection; this
is not a claim that every credential or vulnerability has been detected.
After activation, dependency-graph SBOM retrieval succeeded and listed five
root-workflow action dependencies plus the repository. The composite-action
inputs, pre-commit hooks, distribution packages and shaders were not represented
in that graph, so its coverage is explicitly incomplete. Gitleaks and reviewed
version updates remain in place. Additional secret-pattern assessment remains
open; CodeQL and dependency review were open at this activation and are
implemented in the phase 2 record above. `SECURITY.md` records the private
reporting route, current-development support and these coverage limits.

The public Advisories page returned HTTP 200 and contained its private-report
link. Both documentation hook stages passed in the maintained container.
No vulnerability report or test credential was submitted.

PR #9 merged through owner-enabled auto-merge after its form-privacy correction.
CodeRabbit review `5247213008` on PR #10 identified the outdated next-step
summary. Updated it to reflect enabled native security controls and the
remaining form, CodeQL and dependency-review work.

Documentation audit readback, 2026-09-19: the repository remains public;
provider secret scanning, push protection, Dependabot security updates and
private vulnerability reporting report enabled. Non-provider patterns and
validity checks remain disabled. Auto-merge availability and merged-branch
deletion report enabled; these settings do not authorize an agent to merge.
