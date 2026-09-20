<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# kwin-effect-upscale

**KWin-native game upscaling for KDE Plasma — designed for launching games
normally, including from Steam, without wrapping them in gamescope.**

`kwin-effect-upscale` is an effect plugin for KWin, the compositor KDE Plasma
already runs. It is loaded by that compositor and does its work inside it:
there is no separate program to start, no service to run and nothing wrapped
around the game. Installing the package is the whole of the setup, and the
effect is enabled once it is installed.

> [!WARNING]
> **Working alpha — it works, it is not finished.** The effect can make a game
> render at a lower resolution and upscale the result to the physical display.
> On real hardware, SuperTuxKart produced up to **87% more frames per second**
> with a smaller render target while KWin still presented the result on the same
> 3840 x 2160 display. See [Measured](#measured).
>
> Image quality, HDR, VRR and broad game compatibility still require more
> real-world testing, and its settings may still change.

[![CI](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml)
[![Nightly](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/JensKSP/kwin-effect-upscale?utm_source=oss&utm_medium=github&utm_campaign=JensKSP%2Fkwin-effect-upscale&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)

## TL;DR

This project brings game upscaling to KDE: games can render at a lower
resolution for better performance while still filling your display at its
native resolution, such as 4K — similar to the upscaling experience Windows
gamers are used to, but integrated directly into KWin.

## Why this project?

KDE Plasma is my desktop of choice, and I also use my PC for gaming with Steam.
A common solution for game upscaling on Linux is to run the game through
[gamescope](https://github.com/ValveSoftware/gamescope). Gamescope is an
excellent project and an important technical reference for this one, but using
it for scaling also means putting another compositor into the game path.

That raised a simple question:

> **If KWin is already compositing the desktop, why can't KWin upscale the game
> itself?**

`kwin-effect-upscale` explores that idea.

Instead of wrapping a game in another compositor, the game stays on the normal
Plasma desktop. KWin identifies configured applications, tries to obtain a
smaller game buffer where possible, then scales that image to the physical
output.

The point is not to reimplement gamescope feature for feature. The goal is a
KDE-native gaming path where spatial upscaling feels like part of the desktop
rather than a separate launch environment.

## The experience we want

Eventually the normal workflow should be:

1. Install the matching package.
2. Start the game normally — including directly from Steam.
3. Play.

The effect should take care of the rest. It is enabled by the package, and the
profiles it ships decide what a recognised game is asked to render, so there is
no preset to choose and no configuration to write before it does anything.

There is plenty to tune for those who want to: a global preset, your own
profile for a game the package does not yet know, per-display rules, a pixel
threshold that decides which outputs are worth scaling at all, and a diagnostic
display reporting what a game actually drew. These are **power-user features**
— worth having, and never a step between installing the package and playing.

A maintained catalogue of well-known games should eventually provide tested
settings for common combinations of game, hardware and display.

Installation and updates should be equally ordinary. The distribution package
manager should handle dependencies and compatible versions, without hand-edited
configuration files, manual dependency hunting or fragile launch recipes.

**This is the destination, not the current state.** The alpha already has
editable application profiles and working resolution-control paths, but broad
game compatibility and full real-device acceptance remain work to be done.

## What upscaling buys you

A 3840 x 2160 display contains four times as many pixels as 1920 x 1080.
Rendering all of those pixels can become the limiting factor for a GPU.

If a game instead renders a smaller image, it may be able to produce frames
faster. The image still has to fill the physical display, so it must be scaled
back up. Basic stretching can look soft; a spatial upscaler tries to preserve
edges and useful detail while enlarging the frame.

This project currently implements:

- **FSR 1 / EASU** for spatial upscaling;
- optional **RCAS** sharpening.

Upscaling is not free, but the extra GPU work can be much smaller than
rendering the game at the display's native resolution.

It also cannot help every game. If the game is limited by the CPU, an internal
frame cap, simulation work or something else unrelated to pixel rendering,
lowering the render resolution may produce little or no frame-rate gain.

Because this effect works on the game's finished image, it scales the whole
frame, including menus, text and HUD elements. An upscaler integrated directly
into a game can instead upscale only the 3D scene and draw its user interface at
native resolution.

## The important part: making the game render smaller

Upscaling a game after it has already rendered a full-resolution frame does not
save rendering work. By the time KWin receives a 3840 x 2160 frame, the game
has already paid the cost of drawing those pixels.

For this effect to improve performance, two separate things have to happen:

1. the game has to produce a smaller image;
2. KWin has to upscale that image to the physical display.

The second step is the easy part. The first one depends on how the game talks
to the desktop.

### Native Wayland games

For a configured native Wayland application, the effect can advertise a
different output mode to that application's Wayland connection.

For example, a game running on a physical 3840 x 2160 display can be presented
with a 2560 x 1440 or 1920 x 1080 mode. Other applications continue to see the
real display mode.

If the game follows the advertised mode, it renders the smaller image and KWin
can upscale it.

### Games running through Xwayland

X11 applications cannot be given isolated output modes in the same way because
they share one X11 display connection.

For configured Xwayland games, the effect instead resizes the game window to
the requested rendering size and presents the result at fullscreen size.

### The game still has the final say

The effect can provide a different rendering environment, but it cannot force a
game's renderer to behave in a particular way.

A game may:

- follow the requested size;
- select another resolution;
- use its own internal render scale;
- impose its own frame-rate limit;
- ignore the request entirely.

If it continues rendering at the native output size, there is nothing useful
for this effect to upscale.

That is why compatibility has to be established game by game.

Two consequences are worth knowing:

- **A game may report a resolution you did not manually choose.** Its settings
  show the mode it was offered, because from the game's point of view that is
  the display mode.
- **Applications that are not configured are left alone.** The effect ships a
  small set of known applications and allows additional profiles to be added.

Upscaling itself remains separate from resolution control: the effect can
enlarge a smaller fullscreen image whether that size was requested by the
effect or chosen by the application itself.

## Steam and game compatibility

**Steam is a primary target for this project.** The intended user experience is
that games can be started normally from Steam rather than through a special
upscaling wrapper.

There is an important complication: Steam does not define the display path the
game will actually use. Depending on the game and its runtime, it may reach
KWin as a native Wayland client or through Xwayland, and bundled libraries can
change which backends are available.

In particular, many Steam titles ship their own SDL rather than using the SDL
provided by the Linux distribution. That means a backend switch that works for
a distribution package is not automatically available to a Steam build of the
same software.

For the current alpha, compatibility therefore means testing the actual game
and confirming two things:

1. whether the game really renders at the requested size;
2. whether it reaches KWin through the display path we expect.

Broad Steam and Proton coverage is still future work. A maintained game
catalogue is part of the long-term plan so that users should not have to reason
about Wayland, Xwayland, SDL or individual engine behaviour themselves.

## Current state

**Working alpha.** FSR 1 and optional RCAS are implemented, and the complete
path has been measured on a physical display. SuperTuxKart was asked for a
smaller buffer, supplied it, and the effect upscaled it to a 3840 x 2160 output
while the game's own frame production increased substantially.

Implemented today:

- FSR 1 / EASU upscaling;
- optional RCAS sharpening;
- global scaling presets;
- editable application profiles;
- targeted native Wayland resolution requests;
- Xwayland resolution handling;
- requested-resolution versus supplied-buffer tracking;
- selected borderless-window handling when content exactly covers one output;
- per-display application rules;
- configurable global output threshold and per-application overrides;
- a Native application rule that bypasses upscaling when the global Native
  preset is selected.

By default, outputs at or below 2,073,600 physical pixels (Full HD) bypass
upscaling.

Verified so far:

- a native Wayland application can be given a smaller rendering target;
- KWin receives the smaller image;
- the effect upscales it to a physical 3840 x 2160 display;
- the Xwayland path works end to end with Extreme Tux Racer;
- virtual sessions verify resolution changes and client isolation.

Still requiring broader real-world acceptance:

- image quality;
- HDR;
- VRR;
- HDR and VRR together;
- physical input behaviour;
- more GPUs and displays;
- televisions;
- more native Wayland games;
- more Xwayland games;
- Steam titles;
- Proton/Wine titles.

Performance numbers below measure frame production, not image quality, power
consumption or representative performance across games.

The [developer handbook](doc/upscaling.md#supported-scope-and-full-acceptance)
defines the remaining acceptance criteria in detail.

## Measured

SuperTuxKart was tested on an NVIDIA workstation with a 3840 x 2160 output at
240 Hz, Wayland, KWin 6.3.6 and effect build
`0.1.0+git20260920.5666b9422b`. The machine was otherwise idle at a load of
0.23. Each row contains 30 samples taken over 60 seconds after a 10-second
warm-up.

| Preset | Game renders at | Upscaled | Game's own frames | Presented |
| --- | --- | --- | ---: | ---: |
| native | 3840 x 2160 | no | 491.8/s | 236.8/s |
| quality | 2560 x 1440 | yes | 834.7/s | 237.1/s |
| performance | 1920 x 1080 | yes | 919.1/s | 237.3/s |

Read **Game's own frames**, not **Presented**. The presented rate is pinned near
the display refresh rate in all three runs. What changes is how quickly the
game itself can produce frames.

At `performance`, SuperTuxKart produced **1.87 times as many frames** as at
native resolution while KWin still filled the same 4K display.

Three runs taken hours apart on different builds agreed within a few per cent:

- 492.6 / 833.3 / 949.4;
- 489.6 / 835.2 / 917.6;
- 491.8 / 834.7 / 919.1.

That makes the result useful as evidence that the mechanism works on this
machine. It is still one game, one machine and one test environment rather than
a promise about another system.

The returns also diminish. `quality` renders about 44% of the native pixel
count and gains about 70%; `performance` renders 25% of the native pixel count
and gains about 87%. Cutting the pixel count almost in half again therefore
buys relatively little additional frame production on this machine. Below that
point, something other than pixel rendering is becoming the limiting factor.

Extreme Tux Racer shows the other side of the same result. It supplied all
three requested sizes — 3840 x 2160, 2560 x 1440 and 1920 x 1080 — and the
latter two were upscaled to the physical display through the Xwayland path. Its
frame rate remained 59.8/s in every run because the game is limited to 60 FPS.
A game already at its own cap has nothing to gain from reducing rendering work.

One additional cost is not represented in either measurement: while the effect
is active, it blocks direct scanout, so a game that could otherwise bypass
composition no longer does so.

## Trying it

### Packages

These are development artifacts, not a stable release. The alpha warning at
the top of this document also applies to packaged builds.

Packages are built for two distributions on two architectures:

| Distribution | Filename suffix | Architectures |
| --- | --- | --- |
| Debian Trixie | `trixie` | amd64, arm64 |
| Kubuntu 26.04 LTS | `resolute` | amd64, arm64 |

Pick the package matching your distribution because a KWin effect is built
against the KWin version it is loaded into.

- **Releases:** <https://github.com/JensKSP/kwin-effect-upscale/releases/latest>
- **Nightly:** <https://github.com/JensKSP/kwin-effect-upscale/releases/tag/nightly>

The nightly release is rebuilt from `master` whenever `master` moves.

Install a downloaded package with:

```bash
sudo apt install ./kwin-effect-upscale_<version>.<distribution>_<architecture>.deb
```

Every release also carries the source tarball with its SHA-256 checksum and a
debug-symbol package next to each binary package.

Release assets are covered by a keyless GitHub build attestation, so a download
can be traced back to the workflow and commit that produced it:

```bash
gh attestation verify ./kwin-effect-upscale_<version>.<distribution>_<architecture>.deb \
    --repo JensKSP/kwin-effect-upscale
```

`SHA256SUMS` lists every asset in the release. `provenance.sigstore.json` holds
the signing bundle and is verified separately, so it is not itself listed there.

The package version uses a tilde before the distribution suffix, as Debian
expects, but GitHub release assets cannot preserve that tilde. For example,

```text
0.1.0+git20260917.3d2d99e6a0.trixie_amd64.deb
```

installs as:

```text
0.1.0+git20260917.3d2d99e6a0~trixie
```

### Check that the effect is running

The package enables the effect, so there is nothing to switch on. To confirm it
is loaded in the running Plasma session:

```bash
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.isEffectLoaded upscale
```

A freshly installed plugin is normally picked up immediately. If that reports
`false`, log out and back in.

It appears as **Upscale** under **Appearance** in **System Settings** →
**Desktop Effects**, which is also where it is switched off again. From a
shell, the equivalent of that tick is:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key upscaleEnabled true
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect upscale
```

### Which build am I running?

The plugin identifies itself when KWin loads it, so the journal records the
exact build used in the current session:

```bash
journalctl --user -b -u plasma-kwin_wayland -g upscale | head -1
# upscale 0.1.0+git20260917.ed8f450b4e (branch master), built 2026-09-17T20:50:02Z, Qt 6.8.2
```

A version without a `+git` suffix is a release. Other versions identify the Git
commit they were built from, and `-dirty` means a development build contained
uncommitted changes. Packaged builds report the complete package version,
including the distribution suffix. Nightly source archives retain their
snapshot version without Git.

## Building from source

### Requirements

The effect is built against the KWin installed on the machine and loaded into
it, so the development files must belong to the KWin version that will actually
run the plugin.

The build requires:

- C++23;
- CMake 3.24;
- Qt 6.8;
- KDE Frameworks / ECM 6.13;
- development files for the target KWin.

`debian/control` is the authoritative build-dependency list, including build,
tool and test dependencies.

On Debian and Kubuntu, install them with `mk-build-deps` from `devscripts`
(using `equivs` to create the dependency package):

```bash
sudo mk-build-deps --install --remove debian/control
```

The maintained containers install dependencies from the same file. A cached
image does not update itself, so each one records the `debian/control` it was
built from and the checks refuse to run when the two have diverged, naming the
rebuild rather than failing later on a missing header:

```bash
podman build --pull --build-arg DEPENDENCY_EPOCH="$(date -u +%Y-%m-%d)" \
    -t upscale-check:trixie -f containers/trixie/Containerfile .
```

`DEPENDENCY_EPOCH` is what refreshes the installed packages: it invalidates the
layer that runs `apt-get`, so passing today's date picks up current packages
from an otherwise unchanged Containerfile. CI passes the same value.

Other distributions provide the same components under their own package names.
The CMake package names to look for are `ECM`, `Qt6`, `KF6` and `KWin`.

### Get the source

```bash
git clone https://github.com/JensKSP/kwin-effect-upscale.git
cd kwin-effect-upscale
```

### Build

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Ninja uses its native parallelism. Set `CMAKE_BUILD_PARALLEL_LEVEL` if a machine
needs a lower job limit.

In-source builds are refused. Without `-DCMAKE_BUILD_TYPE`, the project
configures a debug build, which is not what you want for playing games.

### Install

```bash
sudo cmake --install build
```

With `KWIN_BUILD_KCMS=ON` (the default), this installs the effect and its
configuration module:

```text
<prefix>/lib/<multiarch>/qt6/plugins/kwin/effects/plugins/upscale.so
<prefix>/lib/<multiarch>/qt6/plugins/kwin/effects/configs/kwin_upscale_config.so
<sysconfdir>/xdg/kwinupscalerc
```

`kwinupscalerc` holds the effect's own defaults and lands in KDE's
configuration directory, which is `/etc/xdg` for the default `/usr` prefix. A
user's own changes go to a file of the same name in their configuration
directory, which KConfig layers over this one.

The install prefix defaults to the one KDE Frameworks uses. On Debian that is
`/usr`, which is also where Qt and KWin look for plugins.

If you install to another prefix, KWin will not find the plugin unless the
session that starts `kwin_wayland` has `QT_PLUGIN_PATH` pointing at:

```text
<prefix>/lib/<multiarch>/qt6/plugins
```

### Uninstall a source build

```bash
sudo xargs rm -v < build/install_manifest.txt
```

## Technical details

The permanent [developer handbook](doc/upscaling.md) contains the project
requirements, specification and design, including implemented behaviour and
open acceptance criteria.

HDR and variable refresh rate are project requirements, including using both at
the same time while upscaling. Real-device acceptance for those paths remains
open.

This is an independent project and **not an official KDE project**.

## Applications used for development and testing

A useful test application can run fullscreen, can render below the physical
output resolution, and lets us identify which display path it takes.

Debian's SDL2 provides a Wayland backend, so applications linked against the
system SDL2 can often be sent through either backend using `SDL_VIDEODRIVER`.
Applications that bundle their own SDL2 — as many Steam titles do — remain
limited by the backends in that bundled copy.

Extreme Tux Racer is already used for resolution-request testing. The other
entries below are candidates and test tools, not compatibility claims.

| Application | Where it comes from | Display path | Graphics API |
| --- | --- | --- | --- |
| Extreme Tux Racer | `extremetuxracer` | either, via `SDL_VIDEODRIVER` | OpenGL |
| SuperTuxKart | `supertuxkart` | either | OpenGL |
| Taisei | `taisei` | either | OpenGL |
| 0 A.D. | `0ad` | either | Vulkan or OpenGL |
| OpenArena on ioquake3 | `openarena`, `ioquake3` | either | OpenGL |
| Warzone 2100 | `warzone2100` | either | OpenGL |
| Unvanquished | own launcher, or Flathub | Wayland without a switch (SDL 3) | OpenGL |
| Veloren | Airshipper, or Flathub | Wayland without a switch (winit) | Vulkan, through wgpu |

SuperTuxKart accepts both display backend and resolution on the command line,
which makes the intended test case easy to express:

```bash
SDL_VIDEODRIVER=wayland supertuxkart --fullscreen --screensize=1280x720
```

0 A.D. is interesting because Alpha 27 can render through Vulkan and includes
its own FSR implementation, allowing the same scene to be compared with this
effect. Veloren is a useful native Wayland plus Vulkan candidate and also has
an internal render scale.

Left 4 Dead 2 is the native Source engine title this effect's X11 path was
developed against, launched normally from Steam through pressure-vessel. Its
window identifies itself as `hl2_linux`, which is the engine binary rather than
the game, so the profile the package ships covers every native Source title.
Source takes its fullscreen size from the window manager and never asks
Xwayland for a mode, which is the case the effect has to present and map
pointer input for itself.

Project Zomboid is not open source, but it has been useful for observing an
X11/Xwayland game path. Its LWJGL 2 compatibility layer pins GLFW to X11 unless
the system property `zomboid.wayland=1` is set, even though the bundled GLFW
contains both backends. In the observed X11 session, the process mapped
`libX11` and `libGLX`, not `libwayland-client`, and its log showed the XRandR
mode request that Xwayland then emulated.

For any candidate, the display path still has to be verified rather than
assumed. Two useful observations are whether the process has
`libwayland-client` mapped and whether its window appears in Xwayland's window
tree.

## Inspiration and references

The design draws on existing free software and published shader
implementations:

- [gamescope](https://github.com/ValveSoftware/gamescope) demonstrates
  compositor-level game scaling with separate handling of overlays, sharpening
  and output colour management. Its FSR, NIS, SGSR and pixel-filter paths are
  important references for this effect.
- [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR)
  provides the EASU upscaler and RCAS sharpening pass.
- [AMD FidelityFX CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS)
  provides another approach to adaptive sharpening with optional upscaling.
- [NVIDIA Image Scaling](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling)
  combines spatial upscaling and adaptive sharpening and documents requirements
  for SDR and HDR input.
- [Snapdragon Game Super Resolution 1](https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/main/sgsr/v1)
  provides a spatial filter that combines upscaling and sharpening in one
  shader pass, including a GLSL reference implementation.
- [libplacebo](https://github.com/haasn/libplacebo) provides references for
  bicubic and Lanczos filters, including EWA variants and anti-ringing.
- [KWin's own effects](https://invent.kde.org/plasma/kwin/-/tree/master/src/plugins)
  guide the plugin structure and integration. The zoom effect's
  [xBRZ shader](https://invent.kde.org/plasma/kwin/-/blob/master/src/plugins/zoom/shaders/upscaler.frag)
  is also a reference for enlarging pixel graphics.

[Anime4K](https://github.com/bloc97/Anime4K),
[FSRCNNX](https://github.com/igv/FSRCNN-TensorFlow) and
[RAVU](https://github.com/bjin/mpv-prescalers) have also been considered as
additional spatial alternatives.

The current effect implements FSR 1 with optional RCAS. Incorporated
third-party code retains its own copyright and licence notices.

## Notes for packagers

- Warnings are errors by default. Disable that for a distribution build with
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF`.
- `DESTDIR` is honoured: `DESTDIR=/tmp/stage cmake --install build`.
- The plugin declares KWin's effect API version, so it must be **rebuilt after a
  KWin upgrade**.
- Debian packages depend on the exact `kwin-common` version they were built
  against, so a KWin upgrade requires a matching rebuild of this package.
- `debian/` is part of the tree and builds a single binary package with
  `dpkg-buildpackage -b`.
- The source format is native, so no orig tarball is required.
- Build dependencies live in `debian/control`; CI installs them from that file
  with `mk-build-deps`.
- The build honours `SOURCE_DATE_EPOCH`, which debhelper sets from the changelog,
  so packaged builds remain reproducible. No other part of the build reads the
  wall clock.

## Notes for contributors

Contributions are welcome: bug reports, testing on different setups,
documentation improvements and code. Feel free to open an issue or pull request
on GitHub.

The [contributor guide](CONTRIBUTING.md) describes reporting, maintained build
environments, checks and submission expectations.

We use Codex and Claude to help write code for this project. We aim to keep
"AI slop" out: unnecessary abstractions, boilerplate and changes we cannot
explain or verify. The standard is readable code that fits KWin's conventions,
with human review and checks for correctness. Responsibility remains with us.

Documentation under `doc/` is permanent and written for humans. For each major
implementation slice, coding agents keep one temporary working document under
[`doc/agents/`](doc/agents/) with the topic's start state, target state, scope,
dependencies, acceptance criteria, progress, findings, test results and
remaining work.

Once the implementation is complete and all required tests pass — including
real-device acceptance where required — the temporary document is removed.
Lasting requirements and design conclusions belong in permanent documentation;
implementation explanations belong in source comments. Source code, comments,
tests and human documentation together are the source of truth. `AGENTS.md`
instruction files remain permanent.

### Repository checks

Pre-commit defines the repository checks. Install both hooks and run both
stages:

```bash
pipx install pre-commit==4.6.2
pre-commit install --hook-type pre-commit --hook-type pre-push
pre-commit run --all-files
pre-commit run --all-files --hook-stage pre-push
```

Inside the maintained container, this runs both stages exactly as CI does:

```bash
python3 -B tools/run-checks.py lint
```

The commit-stage checks focus on changed files. Pre-push runs whole-tree checks
and regression tests. CI runs both over the repository and adds GCC and Clang
builds, an arm64 build, clang-tidy, metadata-schema checks, coverage,
sanitizers and package and source smoke tests. Nightly additionally runs the full package matrix and
separate KWin-master compatibility builds. Documentation-only changes use the
reduced checked path described in the contributor guide.

The checks cover KDE coding style through `clang-format` and KWin's own
`.clang-format`, CMake formatting and static checks, Markdown linting, spelling
in documentation and comments, REUSE compliance and source-file size limits.
The CMake linter also checks the plugin folder, with formatting rules disabled
there to preserve KWin's style. Gersemi formats only the surrounding project.

Tools under `tools/` are predominantly Python. `ruff` lints and formats them
with all rules enabled, and `mypy --strict` checks their typing.

The source-file budget permits 400 code lines, with warnings above 300.
Comments and blank lines are excluded; multiline strings such as embedded
shaders count. Unreadable or unmeasurable files fail the check. Regression
tests for these rules and the build metadata run in the pre-push stage.

`clang-tidy` requires a configured Clang build. In the maintained container:

```bash
python3 -B tools/run-checks.py tidy
```

CI builds Debian Trixie, the minimum supported environment using KWin 6.3.6,
with both GCC and Clang and with warnings treated as errors. Nightly also builds
against KDE neon unstable, which tracks KWin master. Both environments live
under `containers/` so the same builds can be reproduced locally.

Both images verify CMake, Ninja, GCC and Clang during creation. Ninja comes from
the shared `debian/control` dependencies. An existing local image does not
update itself, so each records the `debian/control` it installed and the checks
stop with a rebuild instruction when it no longer matches the tree. Dependabot
watches the base images and the pinned actions; it does not rebuild anything.

## Releasing

A release is created from a tag; nothing else is performed manually:

```bash
# the tag, project(VERSION) and debian/changelog must agree, or CI stops
git tag -a v0.1.0 -m 'kwin-effect-upscale 0.1.0'
git push origin v0.1.0
```

The release workflow builds packages for both architectures and both supported
distributions, creates the source tarball and publishes them as a GitHub
release with generated notes.

`nightly` is one rolling pre-release rebuilt from `master` whenever `master`
moves. Its tag is deleted and recreated each time, so it is not a stable URL
for a fixed build.

## Repository layout

```text
src/plugins/upscale/     the effect, laid out exactly as KWin lays out its own
cmake/                   stand-ins for KWin's in-tree build macros
containers/              build environments: Trixie minimum, KDE neon unstable
tools/                   checks that run in pre-commit and CI
doc/                     permanent human documentation: what the effect does and why
doc/agents/              temporary implementation documents for coding agents
```

`src/plugins/upscale/` is intended to remain copyable into KWin's own
`src/plugins/` unchanged. Everything specific to building this effect outside
KWin lives elsewhere in the repository.

## Licence

`GPL-2.0-or-later`, REUSE compliant. Third-party shaders keep their own licence.
