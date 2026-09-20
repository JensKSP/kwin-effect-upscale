<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Pipeline modules: build, test, release

## Start state

The pipeline works and publishes, but it is composed of workflows that each
carry a whole stage rather than one step, and two of them do the same work with
different tooling.

- `package.yml` builds the Debian and Ubuntu packages and the source archive;
  `distribution-packages.yml` builds the Fedora, openSUSE and Arch packages.
  Both run *environment, build, install into a clean container, upload* with
  their own copy of each step. `package.yml` is called by `ci.yml`,
  `nightly.yml` and `release.yml`; `distribution-packages.yml` by the latter
  two.
- Job names do not say what a job does. The nightly shows `Build / trixie amd64`
  beside `distribution-packages / opensuse amd64`, although both build a
  distribution package.
- The target list exists three times: the matrix in `package.yml`, the matrix
  in `distribution-packages.yml`, and `DISTRIBUTIONS`, `ARCHITECTURES` and
  `DISTRIBUTION_ARCHITECTURES` in `tools/release_assets.py`.
- Every package is built twice and compared, which only the reproducibility
  requirement needs, and each of those two builds runs the test suite again.
- The nightly calls `ci.yml` in full, on a commit whose master push already ran
  it. Measured on nightly run `35516606893`: 126 runner minutes, of which the
  repeated check leg is about 42 (`tidy` 17, `address` 14, the rest ~11) and the
  second package builds about 15.
- FreeBSD builds and runs two tests through `portability.yml` and produces no
  package, although `tools/distribution_packages.py` already translates
  `debian/control` into FreeBSD package names.

## End state

One set of named modules, composed by the three event entry points into the
stages *build, test, release*, with each target built once.

- Composite actions do one thing each: build a container image, run a check
  group, build one package, test one installed package.
- `build-packages.yml` and `test-packages.yml` take a target list as input.
  A pull request passes one target, the nightly and a release pass all of them.
  `package.yml` and `distribution-packages.yml` are gone.
- Job names read `<Verb> <Target> <Object>`, with the calling job supplying the
  stage: `Build / Debian Trixie amd64 Package`, `Test / Fedora arm64 Package`,
  `Release / Nightly Assets`.
- `tools/ci_targets.py` is the only list of targets. It supplies the CI matrix,
  the display labels, the container base images and the release inventory.
- A package is built once. Only Debian is built twice, for the reproducibility
  comparison, and neither of those builds runs the test suite: the tests run
  once, on the installed package.
- FreeBSD produces an installable `pkg` package, built and installed in the same
  virtual machine.
- Publication depends on the test stage, not on the build stage.

## Scope and boundaries

Owns the composition, naming and cost of the GitHub Actions pipeline, the
target table, the FreeBSD package, and the regression coverage for all of it.

Explicitly excluded:

- **What the checks themselves do.** `run-checks.py` and its modes are moved and
  renamed, not rewritten.
- **Which distributions are packaged.** The set is what the handbook's
  [which release of each distribution](../upscaling.md#which-release-of-each-distribution)
  names; this slice does not add or drop one, except for adding FreeBSD as
  instructed.
- **Publication mechanics**: attestation, the checksum manifest and the draft
  comparison stay as the
  [build and release pipeline](slice-build-release-pipeline.md) specified them.
  This slice changes what `publish` depends on, not what it does.
- **Hardware acceptance.** No new acceptance host appears here.

## Dependencies

The [build and release pipeline](slice-build-release-pipeline.md) owns release
gates, attestation and publication; this slice re-wires which jobs those gates
wait for. `debian/control` remains the only build-dependency list, and every
target keeps translating it through `tools/distribution_packages.py`.

## Gates

**Supported scope this can close against:** the reorganised pipeline produces,
for every target in the table, the same deliverables the current one produces,
verified by a nightly run in which each package is built once, installed in a
clean environment of its distribution and tested there, and by the regression
tests over `ci_targets.py` and the release inventory. For FreeBSD: a package
that `pkg add` installs in the build VM and whose effect and configuration
factories load.

**Full acceptance that stays open:** real-device acceptance on anything but
Debian. FreeBSD in particular has no acceptance host and no second
architecture, so its package is a container-grade artefact and the README and
release notes must continue to name it as untested on hardware. This slice
cannot close that gate.

## Approach

1. `tools/ci_targets.py` with one record per target: id, label, package family,
   container, base image, architectures, whether it is built twice, and which
   tests its package receives. `release_assets.py` imports it instead of
   keeping its own lists. Regression test covers both.
2. Composite actions, renamed to their verb: `build-container-image`,
   `run-checks`, and the new `build-package` and `test-package`.
3. `build-packages.yml` and `test-packages.yml` over the target matrix, plus
   the FreeBSD virtual-machine jobs, which cannot share the container steps.
4. Callers updated: `ci.yml` passes one target, `nightly.yml` and `release.yml`
   pass all of them, and `publish` waits for the test stage.
5. No test runs in a package build; the installed package is tested in a clean
   container of its distribution. The suite stays in the pull request checks:
   see the finding below.
6. FreeBSD packaging: a `pkg create` manifest template under
   `packaging/freebsd/`, filled from `debian/control`, installed and probed in
   the same VM.

## Open decisions

- **The nightly's check leg.** Whether the nightly verifies the commit's CI
  conclusion through the API instead of re-running `ci.yml`. Not implemented
  while undecided; it is the largest single saving.
- **openSUSE.** Jens reported a broken openSUSE amd64 package build. Every
  `opensuse` job in the last ten nightlies succeeded, including the current
  one, so the failure has not been observed here and nothing is being fixed
  blind. Waiting for the run or the symptom.

## Progress

Implemented, not yet exercised by a hosted run.

**The target table.** `tools/ci_targets.py` holds every target with its label,
family, container, image, architectures, whether it is built twice, whether its
package receives the suite, what a clean image needs bootstrapped and what a
nested session needs. `--targets`, `--architectures`, `--only-virtual-machines`
select a matrix. `tools/release_assets.py` derives `DISTRIBUTIONS`,
`ARCHITECTURES` and `DISTRIBUTION_ARCHITECTURES` from it, and carries FreeBSD's
binary pattern; a target that is built but not published now fails a test
rather than a release.

**One entry point per stage.** `tools/build-package.py <target> <version>` and
`tools/test-package.py <target> <package-or-directory>` dispatch on family, so
no workflow branches on packaging. Under them: `build-deb-package.py` (single
build, `DEB_BUILD_OPTIONS=nocheck`, second build only where the table says
reproducible), `build-recipe-package.py` (RPM, Arch and now FreeBSD through
`pkg create` from a staged install), and the two former install tests merged
into one lifecycle check covering all six package managers.

**Four composite actions**, one job's work each and no policy:
`build-container-image`, `run-checks` (now with `build-only`, which is what a
platform that is built but not tested receives), `build-package`, `test-package`.

**Three stages.** `build-packages.yml` and `test-packages.yml` take a target
list; `ci.yml` passes `trixie`/`amd64`, the nightly and `release.yml` pass
everything. `publish` now depends on the test stage. `package.yml`,
`distribution-packages.yml` and `portability.yml` are deleted;
`instrumentation.yml` became `run-instrumented-tests.yml` and `publish.yml`
became `release-assets.yml`, whose provenance check names itself as the signer
workflow. Jobs read `Build / Debian Trixie amd64 Package`,
`Test / Fedora arm64 Package`, `Release`. `Quality gate` keeps its name,
because branch protection requires it by name; `tools/quality_gate.py` and its
test now expect the two package stages instead of `package-smoke`.

**Stable download names.** A release page carries forty files, and the one a
person installs is not the one with the shortest name. Each installable package
is now published a second time under a name carrying neither the version nor the
distribution's release — `kwin-effect-upscale-debian-amd64.deb`,
`kwin-effect-upscale-freebsd-amd64.pkg` — so a link in the README survives both
of them moving. `ci_targets.download_name()` names them,
`release_assets.write_download_aliases()` creates them after validation and
before the manifest, so the checksums and the attestation cover them like
everything else. The README's new Downloads table links them for the latest
release and for the nightly; `publish-release.py`'s guide gained the FreeBSD row
it was missing.

### Observed results

- `python3 -B -m unittest discover -s tools`: **151 tests, OK.**
- Commit stage, `pre-commit run --all-files` in `containers/trixie`: **passed**,
  after one ruff D401 fix in the FreeBSD recipe code.
- Push stage, `pre-commit run --all-files --hook-stage pre-push` in the same
  container: **exit 0, 27 hooks passed**, after two mypy fixes.
- **The first green run did not mean what it looked like.** `pre-commit
  run --all-files` only sees files Git knows about, and the new tools were still
  untracked, so they were skipped. Staging them first produced four real
  findings in them. Every result below is from a run with the whole change set
  staged.
- One of those mypy findings was a real defect, not a typing complaint:
  `run-integration-test.py` reused the name `installed` for both the
  `--installed` flag and the compositor path `shutil.which` returned, so the
  later assignment always won and `QT_PLUGIN_PATH` was dropped on every run,
  including the check jobs that need it to find their own driver module. The
  compositor path is now `system_compositor`.

### Finding: the suite cannot test an installed effect

The test stage first ran the two session tests against the installed effect.
It cannot: `UpscaleEffect::supported()` requires OpenGL, KWin's virtual backend
needs a DRM device for it and a container has none, so the nested session
composites with QPainter and `isEffectSupported("upscale")` is false.
Established by asking a nested session over D-Bus, where `upscale` is listed
among the known effects, reports `supported: false` and fails to load, while
`upscale_test_driver` in the same session reports `supported: true`. That
module exists for exactly this reason, as its own comment says.

Two attempts before that were wrong for smaller reasons and are recorded so
they are not tried again: the tests name the effect `upscale_test_driver`, not
`upscale`, so pointing them at an installed package finds nothing; and
`DEB_BUILD_OPTIONS=nocheck` makes debhelper pass `-DBUILD_TESTING:BOOL=OFF`,
which drops `autotests/` from the package build entirely, so the probe the
clean-container test needs was never built.

What the test stage does establish is that the package installs into a clean
container of its distribution, that both plugins load with every symbol
resolved, and that it reinstalls, removes and purges. The suite keeps running
in the pull request checks on both architectures, where `run-render-tests.py`
drives it through CTest.

### Not verified

Nothing below has run, and none of it may be reported as tested.

- **No package has been built, installed or tested anywhere.** No workflow in
  this slice has executed; every statement about the pipeline above is about
  what the files say, not about a run.
- **The FreeBSD path has never been executed.** There is no FreeBSD here. That
  `pkg create` accepts a version containing `+`, as a snapshot version does, is
  an assumption.
- **The suite against the installed effect has never run.** It is the piece
  most likely to need a second pass: those tests were taken out of the package
  build for timing reasons in the first place.
- **No alias has ever been published.** The stable download names are created
  by code that has only run against test fixtures; the README's links resolve
  to nothing until a nightly publishes them.
- A hosted nightly with `verify-only` is the first check that can close any of
  this, and the measurement of what the reorganisation actually saves.

## Remaining work

- **FreeBSD is tested in the machine that built it**, which has that target's
  build dependencies installed. An undeclared runtime dependency can therefore
  stay available and a broken package pass. The container targets do not have
  this weakness: their test runs in a clean image holding an interpreter and
  the package. Closing it means emptying the machine before the test - delete
  every package, reinstall an interpreter, install the built package and let
  pkg resolve its declared dependencies from the repository - or a jail. Not
  done here: the FreeBSD path has never executed at all, and an unverifiable
  step added to an unverified path is two failures to tell apart. Raised in
  review of this slice.
- Run a verify-only nightly and record what each stage cost and what failed.
- The two open decisions above.
