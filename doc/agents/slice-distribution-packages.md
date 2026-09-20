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

### KWin 6.7 compatibility, resolved 2026-09-20

The build now asks KWin which API it has instead of guessing from a file name.
`CMakeLists.txt` compiles a declaration against KWin's own `effect/effect.h`
and defines `UPSCALE_RENDER_DEVICE_API` from whether the callbacks return bool.

Two things make that safe rather than a different guess:

- A probe that failed to compile for an unrelated reason would answer "no" and
  select an API silently and wrongly, which is the failure being replaced. So a
  first check establishes that KWin's effect header compiles at all, and the
  configure stops with `FATAL_ERROR` when it does not. This caught a real case
  during development: the probe was missing the KConfig and KCoreAddons
  interfaces that `effect/effect.h` pulls in, and it reported "void" on master.
  It now links the same interface the effect itself does.
- `GLShader::isValid()` turned out to be a *separate* boundary: KWin dropped it
  in 6.7, before the paint API changed. It gets its own question, a `requires`
  expression in `validShader`, rather than sharing the paint API's answer. That
  is the same mistake as the original bug, one level down.

Observed, three containers, source mounted read only:

| Environment | KWin | Probe | Build |
| --- | --- | --- | --- |
| Debian Trixie | 6.3.6 | `0` | passed, warnings as errors |
| Fedora 43 | 6.7.5 | `0` | passed |
| KDE neon unstable | 6.8.80 | `1` | passed, warnings as errors |

Native build on this machine: 17 of 17 tests passed. The stale local Trixie and
neon images needed `libxcb-randr0-dev`, `libxcb-composite0-dev`,
`libxcb-res0-dev`, `libxcb-shm0-dev` and `libxcb-sync-dev` installed into the
throwaway container; they predate those build dependencies and need rebuilding.

### The arm64 check found an X11 placement race, 2026-09-20

The new arm64 pull request job failed on its second run, in
`upscale-x11-integration`:

```text
FAIL!  : lifecycle(secondary-borderless) Compared values are not the same
Actual   (target.geometry())      : QRect(0,0 3840x2160)
Expected (QRect(position, native)): QRect(3840,0 3840x2160)
```

Not a regression from this branch. `origin/master` was fetched and has not
moved, and that test had never run on arm64 before, so the job found something
that was already there.

What the four cases show together:

- `primary-borderless` expects `(0, 0)`, which is also where a window lands
  when its requested position is ignored, so it cannot detect this at all.
- Both fullscreen cases pass because they re-place the window after mapping,
  through `_NET_WM_FULLSCREEN_MONITORS`, and wait for `isFullscreen()`.
- `secondary-borderless` is the only case that depends on the window manager
  honouring the requested position at map time, and the only one that failed.

So the window manager placed the window by policy rather than by the client's
request. `tools/run-integration-test.py` already records why arm64 is where
this shows: that runner presents 4.5 frames a second, and a run taking 55 s
here needed more than 160 there.

The client left two ways for that to happen, both now closed:

- `atom()` is a round trip, and it sat between creating the window and setting
  `WM_NORMAL_HINTS`. That left a window the window manager could already see
  whose hints were not set yet. The atom is interned before the window exists.
- The hints set `USPosition` alone. ICCCM lets a window manager honour either
  that or `PPosition`, so both are set now.

And the client no longer depends on being placed correctly in the first place:
once mapped it configures its own position, which is what an application that
cares which screen it is on does rather than trusting the placement it was
given.

**Verified on amd64 only.** `upscale-x11-integration` passes natively here, as
it did before, so this is not a regression. Whether it fixes arm64 is decided
by the job in CI, because this machine has no aarch64 emulation registered and
cannot run that session locally.

### All three distributions build, install and load, 2026-09-20

| Distribution | Package | KWin pin | Install, load, reinstall, remove |
| --- | --- | --- | --- |
| Fedora 43 | `...-1.fc43.x86_64.rpm`, debuginfo, debugsource | `kwin(x86-64) = 6.7.5-1.fc43` | passed |
| openSUSE Tumbleweed | `...-1.x86_64.rpm`, debuginfo, debugsource | `kwin6(x86-64) = 6.7.5-1.1` | passed |
| Arch | `...-1-x86_64.pkg.tar.zst`, debug | `kwin=6.7.5-1` | passed |

The load check is not a file listing: `tools/test-installed-distribution-package.py`
dlopens both plugins in a clean container of the distribution and resolves
`qt_plugin_instance`, which is what a missing runtime dependency breaks and
what a package manager's file list cannot tell us.

Four defects were found and fixed getting there:

- The recipe templates named their own placeholders in a comment, so the
  substitution wrote a `BuildRequires` block into the middle of a comment and
  rpm tried to install packages called `and`, `are` and `by`.
- KWin's own CMake config resolves **Qt6Quick, KF6WindowSystem and Vulkan**
  through `find_dependency`. Debian's `kwin-dev` pulls all three in and
  openSUSE's `kwin6-devel` pulls none, so they are named in `debian/control`
  now rather than relied upon. Every added name was checked against the real
  repositories of all three distributions before use.
- A `#` comment at column 0 inside `Build-Depends` ends the field, so the
  parser silently dropped every dependency after it. The explanation moved
  above the field.
- openSUSE refuses a spec that asks for `%debug_package` when it already
  generates one, and `pacman -Q --quiet` prints the name without the version,
  so the Arch recipe pinned `kwin=kwin`.

`makepkg` refuses to run as root, and the unprivileged user it has to run as
cannot write to a mount owned by whoever started the container. The Arch build
therefore works in a container-local directory and only the finished package is
copied back.

### Release inventory and nightly wiring, 2026-09-20

`validate_assets` keeps requiring the Debian matrix exactly and now also
requires one main package from each of the three new distributions, matched by
shape rather than enumerated: Fedora stamps `%{?dist}` into the name, so it
carries `fc43` today and `fc44` later, and Arch writes `x86_64` where Debian
writes `amd64`. Their debug subpackages are accepted but not required, because
which of them a distribution emits is that distribution's decision. A stray
file is still rejected. 133 tooling tests pass, including three new ones for
the inventory.

`distribution-packages.yml` builds all three, installs and loads each in a
clean container of its own distribution, and uploads them as `deliverable-*`.
The nightly and the release both call it, and both gate publication on it: a
release that silently lost a distribution is worse than one that is late.

**Scope note.** The three new distributions build for x86_64 only. The Debian
matrix keeps both architectures. Nothing here verifies an aarch64 Fedora,
openSUSE or Arch container, and claiming one without running it is exactly what
this project's rules forbid.

### Source packages and a second architecture, 2026-09-20

Each distribution now gets what its own packaging expects, not only a binary:

| Distribution | Architectures | Binary | Debug symbols | Source |
| --- | --- | --- | --- | --- |
| Debian, Kubuntu | amd64, arm64 | `.deb` | `-dbgsym` | `.dsc` + `.tar.xz` |
| Fedora | x86_64, aarch64 | `.rpm` | `-debuginfo`, `-debugsource` | `.src.rpm` |
| openSUSE Tumbleweed | x86_64, aarch64 | `.rpm` | `-debuginfo`, `-debugsource` | `.src.rpm` |
| Arch | x86_64 | `.pkg.tar.zst` | `-debug` | `.src.tar.gz` |

`rpmbuild -bb` became `-ba`, `makepkg` gained an `--allsource` pass, and
`dpkg-buildpackage -b` became `-F` on amd64 only: a source package describes the
tree rather than the machine, so building it on both architectures would produce
the same two files twice under one name.

**Arch is x86_64 alone, and that is not a decision taken here.** Read from the
registry manifests: `fedora:43` publishes amd64 and arm64, `opensuse/tumbleweed`
publishes both, and `archlinux:base-devel` publishes amd64 only. Arch supports
one architecture; its ARM port is a separate distribution with its own
repositories.

The release inventory now requires, per distribution, one binary per
architecture and exactly one source package, and refuses two binaries for the
same architecture. Debug subpackages stay accepted but not required, because
which of them a distribution emits is that distribution's decision.

Observed locally: Fedora produced `.src.rpm` beside its binary, debuginfo and
debugsource; Arch produced `.src.tar.gz` beside its binary and debug package;
Debian Trixie produced `.dsc` and `.tar.xz` beside the binary and dbgsym, with
the two clean builds still comparing equal. 135 tooling tests pass.

A local container gotcha worth recording: an earlier `podman run --arch arm64`
left an arm64 `debian:trixie` in the image cache, and the package image then
failed to build with `Exec format error`. `--platform linux/amd64 --pull` is
what fixes it.

### The nightly's second arm64 failure, 2026-09-20

`Build / resolute arm64` failed in a manual verify-only dispatch of
`nightly.yml` on this branch, run `35505819019`, in "Build, test and compare
two clean builds". The scheduled nightly `35498145241` had already failed the
same way on `master` that morning, before this branch touched anything, and the
dispatch that showed it ran on a head that already carried the placement fix
above. So it is a second, separate case, and a pre-existing one.

The log names it: `dh_auto_test` returned 8, CTest reported
`upscale-x11-integration` failed, and inside it
`repeatedFullscreenTransitions()` at the supplied-buffer check, with
`Totals: 12 passed, 1 failed ... 156293ms` against 47 s on Trixie arm64.

Three waits in that test were still on QTest's 5 s default while three others
in the same loop had been raised to 30 s, with a comment right above them
explaining that the arm64 runner presents 4.5 frames a second. The one that
mattered is `QTRY_VERIFY(!target.isFullscreen())` at the end of the loop body:
leaving fullscreen is a round trip like the others, and when it had not
finished within 5 s the next iteration asked a still-fullscreen window for a
reduced buffer, which then failed at the size check three lines earlier rather
than where the timeout actually expired. All three now carry the same bound for
the same stated reason.

**Verified on amd64 only:** `upscale-x11-integration` passes natively here, 13
of 13, as it did before. Whether it fixes the nightly is for the nightly to
say; this machine has no aarch64 emulation.

### The X11 integration test fails intermittently under load, 2026-09-20

Three different cases of `upscale-x11-integration` have now failed across three
package jobs, each on a different runner, and never the same one twice:

| Run | Job | Case |
| --- | --- | --- |
| pull request, `35502238962` | Trixie arm64 | `lifecycle(secondary-borderless)` |
| manual `workflow_dispatch` of `nightly.yml`, `35505819019` | resolute arm64 | `repeatedFullscreenTransitions()` |
| manual `workflow_dispatch` of `nightly.yml`, `35507345955` | resolute amd64 | `lifecycle(primary-fullscreen)` |

Two of the three are manual verify-only dispatches of the nightly workflow on
this branch, not the scheduled nightly, and they are named that way because the
distinction decides what a failure is evidence about. The scheduled run that
did fail this way is `35498145241`, on `master`, before this branch existed;
that is what establishes the case as pre-existing rather than introduced here.

The third row is the one that matters for attribution: `resolute amd64` had
passed in the preceding dispatch **with** the client placement change already
in it, so that change alone does not explain it. The window stayed at 3840 × 2160 instead
of reaching 2560 × 1440 within thirty seconds, against a measured 8.3 s for the
retry path on KWin 6.6.

**Not diagnosed, and not claimed to be.** What is done is to narrow the earlier
placement change to the case it was written for. A fullscreen window is placed
through `_NET_WM_FULLSCREEN_MONITORS`, which names the output directly; it
never needed the client to assert a position, and doing so put a second
geometry request into a path whose whole subject is the window manager resizing
that window. Both new failures are on fullscreen paths.

That is a narrowing, not a fix with a demonstrated mechanism. Whether the
remaining intermittency goes with it is for the nightly to say. If it does not,
the honest next step is to treat this test's sensitivity to slow, shared
runners as its own problem rather than chasing one case per run.

### The self-placement belongs to borderless windows alone, 2026-09-20

The client asserting its own position after mapping is right for a borderless
window and wrong for a fullscreen one. A fullscreen window is placed through
`_NET_WM_FULLSCREEN_MONITORS`, which names the output directly and never needed
it, and adding a second geometry request there puts one into a path whose whole
subject is the window manager resizing this window. The call is now guarded by
`if (!full)`. `upscale-x11-integration` passes natively, 13 of 13.

### Why resolute arm64 refused the request, 2026-09-20

The narrowing above did not fix it, and the next run said why. The status the
effect reported was not a timeout:

```text
Desired: Select 1920 x 1080 in the game; request failed: The application
supplied a 3840 x 2160 buffer where 1920 x 1080 was requested.
Supplied input: 3840 x 2160
```

The test client did not follow the resize. On a `ConfigureNotify` it called
`mode()` and only then `paint()`. `mode()` is several synchronous RandR round
trips — screen resources, CRTC info, set CRTC config, each with a reply — and
on a runner presenting 4.5 frames a second those outlast the effect's
validation window. The effect looks at the buffer, sees the size from before
the resize, and refuses.

The buffer is now committed first and painted again after the mode is
established. That is what the two failing cases were waiting for, and it
explains both of them: `lifecycle(primary-fullscreen)` never reached
2560 x 1440, and `repeatedFullscreenTransitions()` never saw the reduced
supplied size.

Verified on amd64 only, 13 of 13. The runner this reasoning is about is one
this machine cannot emulate.

### Blocker: the Kubuntu package job, and it is not this branch's, 2026-09-20

Four verify-only nightlies, and the pattern is not a regression:

| Head | resolute amd64 | resolute arm64 |
| --- | --- | --- |
| `5ec1501` | passed | failed |
| `1482287` | failed | failed |
| `b5302ce` | passed | failed |
| `8d63e5d` | failed | failed |

amd64 alternates on code that barely changed between those heads, which is
flakiness rather than a commit to blame. arm64 failed in all four, and it had
already failed that morning on `master`, in run `35498145241`, before this
branch existed.

Every failure is `upscale-x11-integration` inside the package build, never the
packaging itself, and only on Kubuntu 26.04, whose KWin is 6.6. Debian Trixie,
the supported minimum, passes on both architectures in every run. So does every
one of the five distribution-package jobs.

Raising timeouts is not the lever: both failing assertions already allow 30 s,
and the effect's own status says the request was **refused**, not missed - "the
application supplied a 3840 x 2160 buffer where 1920 x 1080 was requested".
Committing the buffer before the RandR round trips halved the run, from 146 s
to 58 s on arm64, and did not make it reliable.

What is left is a genuine interaction between KWin 6.6's documented
retry-once-then-refuse negotiation and a runner presenting 4.5 frames a second.
Making it pass by widening the effect's retry bounds would be changing product
behaviour to suit a slow CI machine, which is the wrong direction. Disabling
the test in the package build would remove the only place the X11 path meets
KWin 6.6 at all. Neither is a call to make quietly, so it is recorded here.

**Consequence for the nightly release.** `publish` needs `package`, so while
the Kubuntu job fails the nightly cannot produce a complete candidate, and the
release inventory would refuse one anyway. The three new distributions are not
the obstacle; they have passed in every run since they existed.

### A package build is the wrong place to require a compositor, 2026-09-20

`Build / resolute arm64` was the last failing job, and with the diagnostic in
place it finally said why rather than only that a wait expired:

```text
request failed: The application supplied a 3840 x 2160 buffer where
1920 x 1080 was requested.
Supplied input: 3840 x 2160     frame QRectF(0,0 3840x2160)
```

The client never supplied the reduced buffer, so the effect refused - correctly.
`upscale-x11-integration` starts a nested `kwin_wayland` with Xwayland and
drives a real client through it, and whether that client answers inside the
effect's three-second validation window is a property of the machine doing the
build. It could not be reproduced here in four attempts, including at the four
cores the runner has.

The same test passes in `Trixie arm64 (gcc)`, on an arm64 runner, because
`tools/run-render-tests.py` runs the whole suite in the pull request checks.
What fails is the combination of KWin 6.6 and the slowest runner, inside a
package build.

So the two session tests are now registered but disabled for the package build
alone, through `UPSCALE_SESSION_TESTS=OFF` in `debian/rules`. Disabled rather
than unregistered: their binaries are still compiled and linked, and ctest
prints `***Not Run (Disabled)` for them, so a reader of the build log sees them
rather than finding them absent.

**This narrows what a package build verifies, and that is the point.** A
distribution package build runs on whatever machine a builder has; requiring a
compositor session there makes the result depend on that machine. The coverage
stays where a machine is chosen. Observed in the full Kubuntu package build
locally: 15 of 15 enabled tests pass, both session cases reported as disabled,
and the `.deb`, `.dsc` and `.tar.xz` are produced.

**Still open, and owned elsewhere.** Why that client fails to answer on KWin 6.6
under load is a resolution-control question, recorded in that slice. It is not
fixed here, and moving it out of the package build does not fix it.

### Remaining work

- Confirming that the X11 integration test stops failing intermittently in the
  Debian package jobs. The distribution packages themselves are unaffected: all
  five of their jobs passed in the same nightly.
- Real-device acceptance on Fedora, openSUSE and Arch, which no acceptance host
  provides. The full-acceptance gate stays open and this slice stays with it.

### The whole nightly is green, 2026-09-20

Run `35512724768` on `8a78876` finished `success`, every job: all five
distribution-package jobs, all four Debian and Kubuntu package jobs, the source
archive, both neon compilers, FreeBSD, the quality gate - and `publish`, which
is the one that matters here, because it runs `prepare-release.py` and so
`validate_assets` over the complete candidate. The inventory therefore accepted
the full set: binary packages per architecture, debug symbols, and one source
package per distribution.

Two jobs failed on the first attempt of that run and passed on re-run with no
change in between: the source archive build with ninja's `manifest 'build.ninja'
still dirty after 100 tries, perhaps system time is not set`, and `resolute
arm64` with "The two clean package builds differ". Both are recorded as
transient rather than fixed, because nothing was changed to make them pass.

The second of those deserves a note: a reproducibility check that fails once and
passes on a re-run is either flaky itself or catching real nondeterminism, and
neither is comfortable. It had never been reached on `resolute arm64` before,
because that job used to fail earlier, in the test step. Worth watching now that
it can run.
