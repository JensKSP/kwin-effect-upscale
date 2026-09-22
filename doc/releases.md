<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Versions and releases

These rules say how a build names itself and how a release is made. They
complete the [repository rules](../AGENTS.md) and bind in the same way. Read
them before touching the version, `cmake/GenerateBuildInfo.cmake`, `debian/`,
packaging or a release workflow, and before making a release. The handbook
names the [supported KWin and distribution releases](upscaling.md#versions)
and describes the [pipeline that builds, tests and publishes](upscaling.md#build-test-release).

## Versions name a commit

Laid down by Jens, 2026-09-17.

- **The version is declared once**, in `project(... VERSION ...)` in the
  top-level `CMakeLists.txt`, set up the KDE way with `ecm_setup_version`. A
  release tag, `debian/changelog` and that number have to agree; the release
  workflow refuses the release when they do not.
- A build away from a release tag calls itself
  `0.1.0+git<commit date>.<short hash>`, with `-dirty` appended when the tree
  had uncommitted changes. That is the Debian ordering for a snapshot taken
  after a release, and the package and the binary carry the same string.
- Branch, commit and build date are compiled in by
  `cmake/GenerateBuildInfo.cmake`, which runs **at build time**, not at
  configure time, so a binary names the commit it was built from rather than the
  one that was checked out when cmake last ran.
- **Run the generator on every build invocation**, including builds without
  source changes and direct builds of the effect or settings target. Recompute
  the revision, ref and timestamp each time; do not reuse a wall-clock timestamp
  just because the sources are unchanged. `SOURCE_DATE_EPOCH` still controls
  reproducible timestamps. Replace the generated file only when its content
  differs.
- **Build information never goes in a header.** It is one generated `.cpp` in the
  build directory, declared by a hand-written header that never changes, so a new
  commit costs one recompiled translation unit and a link. Putting the hash in a
  header would drag every source that includes it through the compiler on every
  commit. Measured, not assumed.
- Keep changing values out of embedded plugin metadata and resource inputs as
  well, so they do not retrigger Qt's metadata/resource generation. When values
  change, compile only the small build-information unit and link the affected
  binaries; verify this with incremental build output.
- The generated source lives in the build directory and is never committed.
- Dates come from `string(TIMESTAMP)`, which honours `SOURCE_DATE_EPOCH`. Nothing
  reaches for the wall clock any other way, because a packaged build has to stay
  reproducible.
- The plugin prints its build information once when KWin loads it, so a journal
  from a session always names the build that ran in it.

## Releases are tags

- **Pushing a tag `v<version>` is the whole release procedure.** The workflow
  builds the packages and the source tarball and publishes them. Nothing is
  uploaded by hand, and a release is never made from a working tree.
- `nightly` is one rolling pre-release, rebuilt from master only when master
  moved. Its tag is deleted and recreated each time; do not point anything at it
  that needs a stable URL for a fixed build.
- Packages are built for amd64 and arm64, on Debian Trixie and on Kubuntu 26.04
  LTS. Support the current releases: an interim Ubuntu release is supported for
  nine months, so packaging for one that is already out of support ships
  something nobody can update.
  A KWin effect is built against the KWin it will be loaded into, so a package is
  only valid for the distribution it was built on.
- **Build dependencies are listed in `debian/control` and nowhere else.** CI
  installs them from that file with `mk-build-deps`. A second list is how a build
  passes on Debian and fails on Ubuntu, which is exactly what happened: Debian's
  `kwin-dev` pulls `libdrm-dev` in and Ubuntu's does not.
