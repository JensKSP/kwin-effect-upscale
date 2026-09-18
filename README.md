<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# kwin-effect-upscale

[![CI](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml)
[![Nightly](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml)

A KWin effect that upscales fullscreen windows rendering below the resolution
of the output they cover.

KDE Plasma is my desktop of choice, and I also use my PC for gaming with Steam.
My gaming setup relies on gamescope to upscale games to 4K, but running another
Wayland compositor for that purpose has always felt unnecessary to me. That
prompted me to explore whether KWin could handle the upscaling itself.

This effect grew out of that idea: let games render at a lower resolution and
have KWin bring the image up to the screen's resolution, making this part of
the desktop I already enjoy using. I hope it will prove useful to others who
want the same flexibility when gaming on KDE Plasma.

## Why upscale?

A 4K screen has four times as many pixels as a 1080p screen. Drawing a game at
that resolution can be too much work for a graphics card to keep gameplay
smooth. Lowering the game's resolution can improve the frame rate, but the
image still needs to fill the screen. Simply stretching it can leave it looking
blurry, with less detail and rougher edges.

Upscaling offers a compromise: the game draws a smaller image, then a shader
(a small program running on the graphics card) enlarges it while trying to keep
edges and details clear. This can look better than basic stretching, though it
cannot recover all the detail of a game drawn at full 4K. The extra processing
adds some GPU work, but usually costs much less than rendering the game at 4K.

The saving is mainly in the GPU work needed for each frame. If the CPU is what
limits the game's frame rate, lowering the resolution may do little to help.
A higher frame rate can also keep the GPU fully busy, so upscaling does not
necessarily mean lower power consumption.

This effect aims to upscale the finished game image, including text and menus.
An upscaler built into a game can work on the 3D scene separately and keep the
interface at full resolution.

## Technical details

The permanent [developer handbook](doc/upscaling.md) describes requirements,
specification and design. The current implementation plan, progress, findings
and remaining work are recorded separately in the
[FSR 1 slice](doc/slice-fsr1-hdr-vrr.md).

HDR and variable refresh rate (VRR) support are requirements for the effect,
including their combined use while upscaling. They are part of the acceptance
criteria for the first usable implementation; real-device acceptance remains
open as described below.

This is not an official KDE project.

## Inspiration and references

The design draws on existing free software and published shader implementations:

- [gamescope](https://github.com/ValveSoftware/gamescope) demonstrates scaling
  the game image in the compositor, with separate handling of overlays,
  sharpening and output colour management. Its FSR, NIS, SGSR and pixel-filter
  paths are references for this effect.
- [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR)
  provides the EASU upscaler and RCAS sharpening pass.
  [AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS)
  offers another approach to adaptive sharpening, with optional upscaling.
- [NVIDIA Image Scaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
  combines spatial upscaling and adaptive sharpening and documents their
  requirements for SDR and HDR input.
- [Snapdragon Game Super Resolution 1](https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/main/sgsr/v1)
  provides a spatial filter that combines upscaling and sharpening in one
  shader pass, including a GLSL reference implementation.
- [libplacebo](https://github.com/haasn/libplacebo) provides references for
  bicubic and Lanczos filters, including EWA variants and anti-ringing.
- [KWin's own effects](https://invent.kde.org/plasma/kwin/-/tree/master/src/plugins)
  guide the plugin structure and integration. The zoom effect's
  [xBRZ shader](https://invent.kde.org/plasma/kwin/-/blob/master/src/plugins/zoom/shaders/upscaler.frag)
  is also a reference for enlarging pixel graphics.

We also considered [Anime4K](https://github.com/bloc97/Anime4K),
[FSRCNNX](https://github.com/igv/FSRCNN-TensorFlow) and
[RAVU](https://github.com/bjin/mpv-prescalers) as further spatial alternatives.
These references describe the work studied so far. The effect implements FSR 1
with optional RCAS; incorporated third-party code retains its own copyright
and licence notices.

## State

FSR 1 scaling, optional RCAS sharpening and resolution controls are implemented.
The controls show the desired resolution and the actual supplied buffer size;
game resolution changes currently require the game's own settings. Automated
rendering and configuration tests cover the implementation, but full compositor
lifecycle, real-game, HDR and VRR acceptance remain open. The effect is disabled
by default.

## Packages

Packages are built for amd64 and arm64, for Debian Trixie and for Kubuntu
26.04 LTS. Pick the one matching the distribution you run, because a KWin effect is
built against the KWin it is loaded into.

- **Releases:** <https://github.com/JensKSP/kwin-effect-upscale/releases/latest>
- **Nightly**, rebuilt from master whenever master moves:
  <https://github.com/JensKSP/kwin-effect-upscale/releases/tag/nightly>

```bash
sudo apt install ./kwin-effect-upscale_<version>.<distribution>_<architecture>.deb
```

Every release also carries the source tarball with its SHA-256 checksum, and a
debug symbol package next to each binary one.

The package version separates the distribution with a tilde, as Debian does, but
a release asset cannot carry one: GitHub rewrites it to a dot when the file is
uploaded. `0.1.0+git20260917.3d2d99e6a0.trixie_amd64.deb` therefore installs as
version `0.1.0+git20260917.3d2d99e6a0~trixie`.

### Which build am I running?

The plugin names itself when KWin loads it, so a journal always says exactly
which build was in the session:

```bash
journalctl --user -b -u plasma-kwin_wayland -g upscale | head -1
# upscale 0.1.0+git20260917.ed8f450b4e (branch master), built 2026-09-17T20:50:02Z, Qt 6.8.2
```

A version with no `+git` suffix is a release; anything else names the commit it
was built from, and `-dirty` means a development build had uncommitted changes.
Packaged builds report the complete package version, including the distribution
suffix. Nightly source archives retain their snapshot version without Git.

## Requirements

The effect is built against the KWin installed on the machine and loaded into
it, so the development files have to belong to the KWin that is actually run.

| What | Version | Debian Trixie package |
| --- | --- | --- |
| C++ compiler with C++23 | GCC 14 or Clang 19 (both are used here) | `build-essential`, or `clang` |
| CMake | 3.24 | `cmake` |
| extra-cmake-modules | 6.13 | `extra-cmake-modules` |
| Qt 6: Core, Gui, Widgets, DBus, OpenGL | 6.8 | `qt6-base-dev` |
| KDE Frameworks 6: Config, CoreAddons, I18n | 6.13 | `libkf6config-dev`, `libkf6coreaddons-dev`, `libkf6i18n-dev` |
| KWin development files | the KWin you run | `kwin-dev` |
| git, to get the source | — | `git` |
| libepoxy (KWin's OpenGL headers need it) | — | `libepoxy-dev` |
| libdrm, Wayland and xkbcommon headers | — | `libdrm-dev`, `libwayland-dev`, `libxkbcommon-dev` |
| pkg-config, used by KWin's CMake config | — | `pkgconf` |
| gettext, for `msgfmt` | — | `gettext` |
| Ninja, optional | — | `ninja-build` |
| clang-format, only to commit changes | 19, the version CI uses | `clang-format` |

On Debian Trixie, on Kubuntu, or on a derivative of either:

```bash
sudo apt install build-essential cmake extra-cmake-modules qt6-base-dev \
    libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev kwin-dev \
    libepoxy-dev libdrm-dev libwayland-dev libxkbcommon-dev pkgconf \
    gettext git
```

The last four are easy to miss on Kubuntu. `KWinConfig.cmake` looks for Libdrm,
Wayland and XKB through pkg-config, and Debian's `kwin-dev` happens to pull
those headers in while Ubuntu's does not. Without them the configure step stops
at `Could NOT find Libdrm`, which sounds like a missing KWin and is not.

If you intend to commit changes, add `clang-format` to that list: the checks
run `clang-format` and refuse the commit without it.

Other distributions ship the same pieces under their own names: the CMake
package names to look for are `ECM`, `Qt6`, `KF6` and `KWin`.

## Getting the source

```bash
git clone https://github.com/JensKSP/kwin-effect-upscale.git
cd kwin-effect-upscale
```

## Building

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

In-source builds are refused; `-B build` is the way. Without
`-DCMAKE_BUILD_TYPE` the project configures a debug build, which is not what
you want for playing games. `-G Ninja` works if Ninja is installed.

## Installing

```bash
sudo cmake --install build
```

That installs the effect and, with `KWIN_BUILD_KCMS=ON` (the default), its
configuration module:

```text
<prefix>/lib/<multiarch>/qt6/plugins/kwin/effects/plugins/upscale.so
<prefix>/lib/<multiarch>/qt6/plugins/kwin/effects/configs/kwin_upscale_config.so
```

The install prefix defaults to the one KDE Frameworks uses, which is `/usr` on
Debian and is where Qt, and therefore KWin, looks for plugins. **If you install
under a different prefix, KWin will not find the plugin** unless the session
that starts `kwin_wayland` has `QT_PLUGIN_PATH` pointing at
`<prefix>/lib/<multiarch>/qt6/plugins`.

## Enabling the effect

The effect ships disabled. In System Settings, open *Desktop Effects* and tick
*Upscale* under *Appearance*. From a shell in the running session:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key upscaleEnabled true
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect upscale
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded upscale
```

A freshly installed plugin is normally picked up straight away; if
`isEffectLoaded` stays `false`, log out and back in.

## Uninstalling

```bash
sudo xargs rm -v < build/install_manifest.txt
```

## Notes for packagers

- Warnings are errors by default. Turn that off for a distribution build with
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF`.
- `DESTDIR` is honoured: `DESTDIR=/tmp/stage cmake --install build`.
- The plugin declares KWin's effect API version, so it has to be **rebuilt
  after a KWin upgrade**.
- Debian packages depend on the exact `kwin-common` version they were built
  against, so a KWin upgrade requires a matching rebuild of this package.
- `debian/` is in the tree and builds a single binary package with
  `dpkg-buildpackage -b`. The source format is native, so no orig tarball is
  needed.
- Build dependencies live in `debian/control` and nowhere else; CI installs them
  from it with `mk-build-deps`.
- The build honours `SOURCE_DATE_EPOCH`, which debhelper sets from the changelog,
  so packaged builds stay reproducible. Nothing in the build reads the wall clock
  any other way.

## Notes for contributors

Contributions are welcome: bug reports, testing on different setups,
documentation improvements and code. Feel free to open an issue or a pull
request on GitHub.

We use Codex and Claude to help write code for this project. We aim to keep
"AI slop" out: unnecessary abstractions, boilerplate and changes we cannot
explain or verify. The standard is readable code that fits KWin's conventions,
with human review and checks for correctness. Responsibility stays with us.

For each major slice, we keep one separate working document under `doc/`:
the plan first, then progress, findings, test results and remaining tasks in
the same file.
Durable explanations belong in source comments. Once implementation and
required testing are complete, we remove that slice's working document and its
links. The developer handbook remains and is kept current with requirements
and design conclusions. Code, comments and tests specify the implemented
behaviour.

Every check in this repository runs from one command:

```bash
pip install pre-commit    # or: pipx install pre-commit
pre-commit install --hook-type pre-commit --hook-type pre-push
pre-commit run --all-files
```

Linters run when you commit and look at what changed; the whole-tree checks and
the regression tests run when you push. CI runs both over everything, adds a
build with GCC and with Clang, clang-tidy and the plugin metadata schema, and
leaves the packages and the build against KWin master to the nightly.

That covers KDE's coding style via `clang-format` and KWin's own
`.clang-format`, CMake formatting and static checks, Markdown linting, spelling
in documentation and comments, REUSE compliance and a limit on how large a
source file may grow. CI runs the same command, because a hook can be skipped.
The CMake linter also checks the plugin folder, with its formatting rules
disabled to preserve KWin's style. Gersemi formats only the surrounding project.

The tooling under `tools/` is Python, and it is the only language here besides
C++ and CMake. `ruff` lints and formats it with every rule switched on, and
`mypy --strict` type checks it, so an annotation is both required and true.

The file budget allows 400 code lines, with warnings above 300. Comments and
blank lines are excluded; multiline strings such as embedded shaders count.
Files that cannot be read or measured fail the check. Regression tests for
these checks and the build metadata run through the same pre-commit command.
Install the hook to enforce the checks on ordinary commits; require the CI
check in branch protection to enforce them when merging.

`clang-tidy` is separate because it needs a configured build:

```bash
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/plugins/upscale/*.cpp
```

CI builds Debian Trixie, the minimum supported environment (KWin 6.3.6), with
GCC and with Clang and with warnings as errors. The nightly additionally builds
against KDE neon unstable, which tracks KWin master. Both environments are
defined under `containers/` so the same build can be reproduced locally.
Both images verify CMake, Ninja, GCC and Clang during image creation. Ninja
is installed from the shared `debian/control` dependencies. Rebuild images
after changing these dependencies; an existing local image does not update
when a Containerfile changes.

## Releasing

A release is a tag, and nothing else is done by hand:

```bash
# the tag, project(VERSION) and debian/changelog must agree, or CI stops
git tag -a v0.1.0 -m 'kwin-effect-upscale 0.1.0'
git push origin v0.1.0
```

The workflow builds the packages for both architectures and both distributions,
builds the source tarball, and publishes them as a GitHub release with generated
notes. `nightly` is one rolling pre-release rebuilt from master whenever master
moves; its tag is deleted and recreated each time, so it is not a stable URL for
a fixed build.

## Layout

```text
src/plugins/upscale/     the effect, laid out exactly as KWin lays out its own
cmake/                   stand-ins for KWin's in-tree build macros
containers/              build environments: Trixie minimum, KDE neon unstable
tools/                   checks that run in the pre-commit hook and in CI
doc/                     what the effect does and why
```

`src/plugins/upscale/` is meant to be copyable into KWin's own `src/plugins/`
unchanged. Everything that is specific to building this outside KWin lives
outside that folder.

## Licence

`GPL-2.0-or-later`, REUSE compliant. Third-party shaders keep their own licence.
