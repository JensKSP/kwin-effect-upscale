<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# kwin-effect-upscale

> [!WARNING]
> **Working alpha — it works, it is not finished.** The effect upscales real
> games on a physical display, and a game given a smaller render target draws
> up to 87% more frames a second ([Measured](#measured)).

[![CI](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/ci.yml)
[![Nightly](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml/badge.svg)](https://github.com/JensKSP/kwin-effect-upscale/actions/workflows/nightly.yml)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/JensKSP/kwin-effect-upscale?utm_source=oss&utm_medium=github&utm_campaign=JensKSP%2Fkwin-effect-upscale&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)

## TL;DR

- **Goal:** upscale smaller fullscreen game buffers inside KWin using FSR 1,
  with optional RCAS sharpening.
- **Current state: working alpha.** FSR 1 processing, editable application
  profiles and targeted Wayland/X11 resolution requests are implemented, and
  the whole path has now been measured end to end on a physical display: a
  game asked for a smaller buffer, rendered into it, and had it upscaled to
  the screen. See [Measured](#measured).
- **Next:** image-quality judgement, HDR and VRR acceptance, and broader client
  compatibility.

- **For now:** alpha means it works and is worth trying, not that it is
  finished. It is disabled by default and the settings are still moving.

## Our vision

**Install the package and enjoy your games.** That is the experience we want
upscaling on KDE to offer: one integrated solution that takes care of the whole
journey, from obtaining a suitable rendering resolution for a normally started
game to presenting it clearly and smoothly on your display.

The goal is a KDE plugin with simple settings, familiar desktop behavior and a
useful on-screen display. A comprehensive, maintained library of well-known
games should provide tested settings suited to the game, hardware and display,
so good results come from sensible defaults rather than repeated trial and
error. Advanced controls should be available when wanted, without becoming
homework for everyone else.

Installation and updates should be equally straightforward. The distribution's
package manager should handle dependencies and compatible versions, with no
hand-edited configuration files, manual dependency hunting or fragile setup
recipes. Ideally, installing the matching Debian package is the only setup a
user needs to perform.

**This is our destination, not the current state.** A small editable catalogue
and cooperative resolution-control paths exist. Broad game compatibility and
full real-hardware acceptance remain work to be done.

## Why this project?

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

## How the game is made to render smaller

A compositor receives finished frames. By the time KWin has a game's image, the
game has already paid for every pixel in it, so enlarging a 4K frame would cost
more work rather than less. For upscaling to save anything, the game has to
draw a smaller image in the first place - and games do not offer a way to be
asked. **So this effect arranges for the game to believe a smaller image is the
right one.** That is worth understanding before installing it, because it is
the part that can surprise you, and the part that decides whether the effect
helps your game at all.

How it does that depends on how the game talks to the desktop:

- **A native Wayland game** is told that the screen it is on has a different
  mode. It sees a 2560 x 1440 or 1920 x 1080 screen where the display is really
  3840 x 2160, chooses that resolution as any game would, and renders into it.
  Only the connection belonging to that game is told this; every other window
  keeps the real screen.
- **A game running through Xwayland** cannot be told that, because all X11
  applications share one connection to the display. Its window is resized
  instead, and the effect presents the result at full screen size itself.

The game decides what to do with what it is told, and that is the whole
limitation. One that follows the advertised mode renders smaller and gains the
frame rate under [Measured](#measured). One that ignores it, picks its own
resolution, or renders through a path that never asks the screen, simply
carries on at full size - and then this effect has nothing to upscale and
changes nothing. Neither outcome is a fault to be fixed by trying harder; it is
a property of the game.

Two consequences follow for anyone using it:

- **A game may report a resolution you did not choose.** Its settings will show
  the size it was offered, because from inside the game that is the truth.
- **Nothing is asked of an application that is not in the list.** The effect
  ships a small set of applications it knows about, and the settings let you
  add your own. Everything else is left alone entirely.

Upscaling itself is separate from all of this: the effect enlarges any smaller
fullscreen image it is given, whether it asked for that size or the game chose
it. Asking is what makes the saving possible; upscaling is what keeps the
result looking like the screen it fills.

## Technical details

The permanent [developer handbook](doc/upscaling.md) describes requirements,
specification and design, including implemented behaviour and open acceptance.

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

**Working alpha.** FSR 1 and optional RCAS are implemented, and the effect has
been measured doing its job on a physical display: SuperTuxKart was asked for a
smaller buffer, supplied it, and the effect upscaled it to a 3840 x 2160 screen
while the game's own frame rate nearly doubled. The numbers are under
[Measured](#measured). What alpha still means here: image quality has not been
judged, HDR and VRR are unverified, and only a handful of applications have
been tried.

The controls distinguish requested resolution from the actual supplied buffer.
Profiles support resolution requests for cooperating Wayland and Xwayland
clients, including Extreme Tux Racer on the primary display. Selected borderless
windows qualify when their content exactly covers one output. Virtual sessions
verify resolution changes and isolation; physical input, image quality, HDR and
VRR remain unverified, and performance is measured only as the frame rates
under [Measured](#measured) - not image quality at speed, not power, and not on
a television. The effect is disabled by default.
Application rules apply independently on each display. By default, outputs at
or below 2,073,600 physical pixels (Full HD) bypass upscaling; the settings offer
a global threshold and per-application overrides. A Native application rule
also bypasses upscaling when a global scaling preset is selected.
The [developer handbook](doc/upscaling.md#supported-scope-and-full-acceptance)
defines the acceptance still required.

## Measured

SuperTuxKart on an NVIDIA workstation, 3840 x 2160 at 240 Hz, Wayland, KWin
6.3.6, effect build `0.1.0+git20260920.5666b9422b`, machine otherwise idle at a
load of 0.23. Each row is 30 samples taken over 60 seconds, after a 10-second
warm-up.

| Preset | Game renders at | Upscaled | Game's own frames | Presented |
| --- | --- | --- | --- | --- |
| native | 3840 x 2160 | no | 491.8/s | 236.8/s |
| quality | 2560 x 1440 | yes | 834.7/s | 237.1/s |
| performance | 1920 x 1080 | yes | 919.1/s | 237.3/s |

Read **Game's own frames**, not **Presented**. The presented rate is pinned at
the screen in all three runs, so it says nothing about the resolution; what
changed is how fast the game itself could produce frames, which is what a
smaller render target buys. At `performance` SuperTuxKart drew 1.87 times as
many frames as at native while still filling the same 4K screen.

Three runs, taken hours apart on different builds, agree to within a few per
cent: 492.6 / 833.3 / 949.4, then 489.6 / 835.2 / 917.6, then the table above.
That is what makes it worth printing, and it is still one machine, one game and
one session rather than a promise about yours.

The returns fall off, and that is worth reading rather than glossing over.
`quality` renders 44% of the pixels and gains 70%; `performance` renders 25% of
them - little more than half as many again - and gains only 87%. Cutting the
pixels further bought almost nothing, so below about 1440p something other than
the pixel count is what limits this game on this machine. A game whose frame
rate is set by its own work on the processor is exactly the case where
upscaling has least to offer, and no amount of it will help.

That headroom is the point where it exists: it is what a game spends on higher
settings, or on staying above a refresh rate it would otherwise miss. Nothing
here measures how the result looks.

Extreme Tux Racer has now been measured at all three presets, and it shows the
other half of the picture. The effect asked it for each size and it supplied
them - 3840 x 2160, then 2560 x 1440, then 1920 x 1080, upscaled to the screen
at the latter two - so the X11 path works end to end through Xwayland, where
the effect resizes the window and presents the result itself. Its frame rate
was 59.8/s in every one of those runs, because the game is frame-limited to 60
and reaches its limit at any resolution. A game already at its cap has nothing
to gain here, and the measurement says so rather than reporting a percentage
nobody can act on.

One cost is in neither table: the effect blocks direct scanout whenever it is
active, so a game that would otherwise bypass composition no longer does.

## Packages

These are development artifacts, not a usable release. The warning above also
applies to packaged builds.

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

The build requires C++23, CMake 3.24, Qt 6.8, KDE Frameworks/ECM 6.13 and
the development files for the KWin being targeted. `debian/control` is the
authoritative build-dependency list, including tools and test dependencies.
After getting the source, install those dependencies on Debian/Kubuntu using
`mk-build-deps` (provided by the distribution’s `devscripts` package, with
`equivs` for building its dependency package):

```bash
sudo mk-build-deps --install --remove debian/control
```

The maintained containers install from the same file. Rebuild a cached image
after changing dependencies; its installed packages do not update themselves.

Other distributions ship the same pieces under their own names: the CMake
package names to look for are `ECM`, `Qt6`, `KF6` and `KWin`.

## Getting the source

```bash
git clone https://github.com/JensKSP/kwin-effect-upscale.git
cd kwin-effect-upscale
```

## Building

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Ninja uses its native parallelism. Set `CMAKE_BUILD_PARALLEL_LEVEL` when a
machine needs a lower job limit.

In-source builds are refused; `-B build` is the way. Without
`-DCMAKE_BUILD_TYPE` the project configures a debug build, which is not what
you want for playing games. The declared build dependencies include Ninja.

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
request on GitHub. The [contributor guide](CONTRIBUTING.md) explains reporting,
maintained build environments, checks and submission expectations.

We use Codex and Claude to help write code for this project. We aim to keep
"AI slop" out: unnecessary abstractions, boilerplate and changes we cannot
explain or verify. The standard is readable code that fits KWin's conventions,
with human review and checks for correctness. Responsibility stays with us.

Documentation under `doc/` is permanent and written for humans. For each major
slice, coding agents keep one temporary working document under
[`doc/agents/`](doc/agents/): one topic with defined start and end states,
scope, dependencies and acceptance criteria, then progress, findings, test
results and remaining tasks in the same file. Once implementation is complete
and all required tests pass, including real-device acceptance where required,
we remove that working document and update its links. Before removal, lasting
requirements and design conclusions go into the permanent documentation and
implementation explanations into source comments. The source code, including
comments and tests, together with human documentation is the single source of
truth. The `AGENTS.md` instruction files remain permanently.

Pre-commit defines every repository check. Install both hooks and run both stages:

```bash
pipx install pre-commit==4.6.2
pre-commit install --hook-type pre-commit --hook-type pre-push
pre-commit run --all-files
pre-commit run --all-files --hook-stage pre-push
```

Inside the maintained container, `python3 -B tools/run-checks.py lint` runs both
stages with one command, exactly as CI does.

Linters run when you commit and look at what changed; the whole-tree checks and
the regression tests run when you push. CI runs both over everything, adds a
build with GCC and with Clang, clang-tidy, the metadata schema, coverage,
sanitizers, and package/source smoke checks. Nightly adds the full package
matrix and the separate KWin-master compatibility builds. Documentation-only
changes take the checked, reduced path described in the contributor guide.

That covers KDE's coding style via `clang-format` and KWin's own
`.clang-format`, CMake formatting and static checks, Markdown linting, spelling
in documentation and comments, REUSE compliance and a limit on how large a
source file may grow. CI runs both stages, because a hook can be skipped.
The CMake linter also checks the plugin folder, with its formatting rules
disabled to preserve KWin's style. Gersemi formats only the surrounding project.

The checking and packaging tools under `tools/` are predominantly Python. `ruff` lints and formats it with every rule switched on, and
`mypy --strict` type checks it, so an annotation is both required and true.

The file budget allows 400 code lines, with warnings above 300. Comments and
blank lines are excluded; multiline strings such as embedded shaders count.
Files that cannot be read or measured fail the check. Regression tests for
these checks and the build metadata run through the pre-push stage.
Install the hook to enforce the checks on ordinary commits; require the CI
check in branch protection to enforce them when merging.

`clang-tidy` needs a configured Clang build. In the maintained container, the
following configures it, runs analysis and validates plugin metadata:

```bash
python3 -B tools/run-checks.py tidy
```

CI builds Debian Trixie, the minimum supported environment (KWin 6.3.6), with
GCC and with Clang and with warnings as errors. The nightly additionally builds
against KDE neon unstable, which tracks KWin master. Both environments are
defined under `containers/` so the same build can be reproduced locally.
Both images verify CMake, Ninja, GCC and Clang during image creation. Ninja
is installed from the shared `debian/control` dependencies. Rebuild images
after changing these dependencies; an existing local image does not update
when a Containerfile changes.

### Applications for testing

An application is useful here when it goes fullscreen, when the resolution it
renders at can be put below the output's, and when we can choose which display
path it takes. Debian's SDL2 carries the Wayland backend, so a game linked
against the system library can be sent down either path with
`SDL_VIDEODRIVER`; a game that bundles its own SDL2, as most Steam titles do,
stays on whatever that copy was built with.

Extreme Tux Racer is already used for resolution requests. The rest are
candidates to evaluate, not results.

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

SuperTuxKart takes both the mode and the resolution on the command line, which
states the case this effect exists for in one line:

```bash
SDL_VIDEODRIVER=wayland supertuxkart --fullscreen --screensize=1280x720
```

0 A.D. is the most interesting of them. Since Alpha 27 it renders through
Vulkan and upscales with its own FSR implementation, so the same scene can be
held against ours. Veloren is the only candidate that is a native Wayland
client and a Vulkan client at once, and it carries a render scale of its own.

Project Zomboid is not open source, and it is the one case measured so far. Its
LWJGL 2 compatibility layer pins GLFW to X11 unless the system property
`zomboid.wayland=1` is set, although the GLFW it ships carries both backends.
Without the property, a session had `libX11` and `libGLX` mapped and no
`libwayland-client`, and its log shows the XRandR mode request that Xwayland
then emulates.

What stays unchecked for every candidate is whether it reaches the compositor
on the path we intended. Two observations settle it: whether the process has
`libwayland-client` mapped, and whether a window for it appears in Xwayland's
window tree.

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
doc/                     permanent human documentation: what the effect does and why
doc/agents/              temporary slice documents for coding agents
```

`src/plugins/upscale/` is meant to be copyable into KWin's own `src/plugins/`
unchanged. Everything that is specific to building this outside KWin lives
outside that folder.

## Licence

`GPL-2.0-or-later`, REUSE compliant. Third-party shaders keep their own licence.
