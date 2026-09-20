<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Distribution packages beyond Debian

## Start state

The pipeline publishes Debian Trixie and Kubuntu 26.04 packages for amd64 and
arm64. `package.yml` owns that matrix; `release.yml` and `nightly.yml` call it
in full, and `ci.yml` calls it with `smoke: true`, which narrows it to Trixie
on amd64. No pull request therefore builds for arm64 at all, although arm64
packages are published.

A FreeBSD job under `portability.yml` builds the effect and runs two tests in
every nightly. It is a portability check and produces no package.

The handbook's [distributions beyond Debian](../upscaling.md#distributions-beyond-debian)
section lists Arch, Fedora and openSUSE as proposed targets, "not decided and
not implemented", and proposes AUR, Copr and OBS as their delivery routes.

## End state

The nightly builds an installable package for Fedora, for openSUSE Tumbleweed
and for Arch, each built against that distribution's own KWin, each verified by
installing it and loading the plugin in a clean container of that distribution,
and each published in the release alongside the Debian packages with the same
checksum manifest and build attestation.

Every one of those builds is reproducible on a developer's machine through a
container under `containers/`, invoked deliberately for that platform and never
as part of the ordinary local check flow.

Pull request checks additionally build for arm64.

## Scope and boundaries

Owns the packaging recipes for the three new distributions, their container
environments, the build and installation-test tooling, their entries in the
release inventory, the nightly wiring, the pull request arm64 build, and the
regression coverage for all of it.

Explicitly excluded:

- **Publication through Copr, OBS or the AUR.** Those need external accounts
  and credentials. The packages are published as release assets through the
  existing attested pipeline, which is the same route the Debian packages take.
- **A FreeBSD binary package.** FreeBSD stays a portability check. It has no
  acceptance host, its packaging format shares nothing with this pipeline, and
  GitHub offers no FreeBSD runner for a second architecture.
- **Hardware acceptance on any of the three distributions.** No acceptance host
  runs them. See the gates below.
- **Rendering, resolution control and game compatibility**, which belong to
  their own slices.

## Dependencies

The [build and release pipeline](slice-build-release-pipeline.md) owns release
gates, attestation, the checksum manifest and publication. This slice adds
targets to the inventory that slice validates; it does not change how a release
is gated or signed.

`debian/control` remains the single build-dependency list. Each new target
translates it into its own package names, the way the FreeBSD job
already does, and fails when a dependency has no mapping.

## Gates

**Supported scope this can close against:** a package for that distribution
that installs into a clean container of it, whose plugin factory loads, that
removes cleanly, and that is built against the KWin that distribution ships and
depends on that exact version. The release documents these three distributions
as built and verified in a container, not as accepted on hardware.

**Full acceptance that stays open:** real-device acceptance on each
distribution, which no acceptance host currently provides. The handbook's rule
that a distribution is not described as supported when no acceptance host runs
it continues to apply, so the README and release notes must name these as
container-verified builds. This slice cannot close that gate and does not claim
to.

## Approach

Follow the existing Debian path rather than inventing a second shape for it.
`tools/build-packages.py` builds in a container, compares two clean builds, and
`tools/test-installed-package.py` installs, loads, reinstalls and removes. The
new targets reuse that sequence with the package manager of each distribution.

Packaging recipes live under `packaging/`, outside `src/plugins/upscale/`,
which must remain copyable into KWin unchanged. `debian/` stays where dpkg
requires it.

Build dependencies are generated into each recipe from `debian/control` at
build time. A `BuildRequires:` list written by hand would be the second
dependency list the repository rules forbid.

Each target pins its KWin the way `debian/rules` does, so a KWin upgrade makes
the package refuse to install rather than load against an ABI it was not built
for.

Nightly runs the three new targets; pull requests do not. The four-level check
model puts expensive, moving or platform-specific work at nightly, and a break
on a distribution the pushing author does not run is not that author's to
answer for. The containers exist so that a break can be reproduced locally when
someone is deliberately working on that platform.

## Acceptance criteria

- The nightly builds Fedora, openSUSE Tumbleweed and Arch packages.
- Each package installs in a clean container of its distribution, its plugin
  factory loads, and removal leaves no plugin libraries behind.
- Each package depends on the exact KWin version it was built against.
- Build dependencies for all targets derive from `debian/control`, and an
  unmapped dependency fails the build with the file to edit named.
- The release inventory accepts exactly the extended asset set, and its
  regression tests cover the new names.
- Each target builds locally through its container with one documented command.
- No new target runs in the commit, push or pull request stages.
- Pull request checks build for arm64.
- Both pre-commit stages pass, including workflow lint and the tooling tests.

## Progress and observed results

### Distribution feasibility probe, 2026-09-20

Ran locally in throwaway containers to establish that each distribution ships
KWin development files the effect's `find_package(KWin REQUIRED COMPONENTS
kwineffects)` can consume. All three do:

| Distribution | Package | KWin | CMake config |
| --- | --- | --- | --- |
| Fedora 43 | `kwin-devel` | 6.7.5, and 6.4.5 also in the repository | `/usr/lib64/cmake/KWin/KWinConfig.cmake` |
| openSUSE Tumbleweed | `kwin6-devel` | 6.7.5 | present |
| Arch | `kwin`, no separate devel package | 6.7.5 | `usr/lib/cmake/KWin/KWinConfig.cmake` |

This establishes that the development files exist. It is not a build: no
package was compiled against any of them.

**Open risk.** All three ship KWin 6.7.5, against a minimum supported target of
6.3.6. `src/plugins/upscale/compatibility.h` separates the region, presentation
and render-device API boundaries with `__has_include`, and the pipeline slice
records Kubuntu 26.04 (KWin 6.6) failing once because one of those boundaries
was assumed to imply the others. KWin 6.7 has not been compiled against here.
Whether the shim covers it is unknown and will be settled by the first build,
not by reading the header.

### Dependency translation, 2026-09-20

`tools/freebsd-packages.py` became `tools/distribution_packages.py`, holding one
table per target, with `tools/distribution-packages.py <distribution>` as the
command each environment calls. `portability.yml` was updated to the new name;
its translated FreeBSD list is unchanged, so the nightly's FreeBSD job sees the
same packages it did before.

The regression tests now check every table rather than FreeBSD's alone,
including that all four tables cover exactly the names in `debian/control`, so
a dependency added for one distribution and forgotten for another fails in the
push stage instead of in a nightly container.

Every translated name was then checked against the real repositories in a
throwaway container of each distribution. Four guesses were wrong and are
corrected:

| Target | Wrong name | Correct name |
| --- | --- | --- |
| Fedora | `kwin-wayland` | `kwin`, since Fedora splits the X11 one off as `kwin-x11` |
| Fedora | `mesa-libGLES-devel` | `libglvnd-gles` |
| Fedora | `mesa-libEGL-devel` | `libglvnd-egl` |
| openSUSE | `extra-cmake-modules` | `kf6-extra-cmake-modules` |

All names for all four distributions now resolve in their own repositories.
Resolving is not installing and not building.

### The effect does not build against KWin 6.7, 2026-09-20

**This blocks every target in this slice.** Fedora 43, openSUSE Tumbleweed and
Arch all ship KWin 6.7.5, and the effect does not compile against it. Observed
in a Fedora 43 container with `kwin-devel-6.7.5-1.fc43`: configure succeeds,
the build fails.

```text
compatibility.h:209: error: void value not ignored as it ought to be
upscale.h:48: error: conflicting return type specified for
    'virtual KWin::UpscalePaintResult KWin::UpscaleEffect::paintScreen(...)'
upscale.cpp:95: error: 'class KWin::RenderView' has no member named 'renderDevice'
upscale.cpp:473: error: could not convert ... from 'void' to 'bool'
```

The cause is the same one the pipeline slice recorded for Kubuntu 26.04: one
API boundary is taken to imply another. `UPSCALE_RENDER_DEVICE_API` is keyed on
`__has_include("core/renderdevice.h")` and then used to select a `bool`
returning paint API. Those two things do not arrive together. Read from the
installed headers of each environment:

| KWin | `core/region.h` | `core/renderdevice.h` | `paintScreen` returns |
| --- | --- | --- | --- |
| 6.3.6, Trixie | no | no | `void`, with `QRegion` and `Output *` |
| 6.7.5, Fedora | yes | yes | **`void`**, with `Region` and `LogicalOutput *` |
| master, neon unstable | yes | yes | `bool` |

KWin 6.7 is therefore an intermediate state the shim has no case for: it has
`core/renderdevice.h`, so the shim selects the `bool` API, but its effect
callbacks still return `void` and its `RenderView` has no `renderDevice()`.

**Proposed fix:** stop inferring the paint API from a header and derive it from
KWin's own declaration, so the compiler answers the question instead of a
heuristic:

```cpp
using UpscalePaintResult = decltype(std::declval<Effect &>().paintScreen(
    std::declval<const RenderTarget &>(), std::declval<const RenderViewport &>(),
    0, std::declval<const UpscaleRegion &>(), std::declval<UpscaleOutput *>()));
```

with the `renderDevice()` and `renderItem()` call sites guarded by a `requires`
expression on the member rather than by the same macro. That removes the class
of bug rather than adding a fourth special case to it.

This is a change to the effect's compatibility layer, not to packaging, and it
has to be verified against all four KWin versions above before it can be
trusted. Whether it belongs in this slice or its own is an open question for
Jens; it is recorded here because this slice cannot proceed without it.

### Remaining work

- KWin 6.7 compatibility, above. Blocks everything else here.
- Packaging recipes, containers, build and install-test tooling, release
  inventory entries and the nightly wiring for all three distributions.

No package has been built for any of the three distributions.
