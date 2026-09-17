<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# kwin-effect-upscale

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

What it does, what it deliberately leaves alone, which scalers can be used and
what is still unanswered about KWin is written down in
[doc/upscaling.md](doc/upscaling.md).

This is not an official KDE project.

## State

Skeleton. The effect builds, loads and does nothing yet.

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
| libepoxy (KWin's OpenGL headers need it) | — | `libepoxy-dev` |
| gettext, for `msgfmt` | — | `gettext` |
| Ninja, optional | — | `ninja-build` |
| clang-format, only to commit changes | 19, the version CI uses | `clang-format` |

On Debian Trixie or a derivative:

```bash
sudo apt install build-essential cmake extra-cmake-modules qt6-base-dev \
    libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev kwin-dev \
    libepoxy-dev gettext git
```

If you intend to commit changes, add `clang-format` to that list: the
pre-commit hook runs `git clang-format` and refuses the commit without it.

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

That installs a single file, the effect plugin:

    <prefix>/lib/<multiarch>/qt6/plugins/kwin/effects/plugins/upscale.so

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
- There is no `debian/` directory in the tree yet.

## Notes for contributors

Configuring on a machine that has the dependencies also installs a pre-commit
hook (ECM's `KDEGitCommitHooks`) checking formatting, the plugin metadata and
the repository rules. CI runs the same checks, because a hook can be skipped.

CI builds in the two container images defined under `containers/`: Debian
Trixie as the minimum supported environment (KWin 6.3.6) and KDE neon unstable
to track KWin master, each with GCC and with Clang and with warnings as errors.

## Layout

    src/plugins/upscale/     the effect, laid out exactly as KWin lays out its own
    cmake/                   stand-ins for KWin's in-tree build macros
    containers/              build environments: Trixie minimum, KDE neon unstable
    tools/                   checks that run in the pre-commit hook and in CI
    doc/                     what the effect does and why

`src/plugins/upscale/` is meant to be copyable into KWin's own `src/plugins/`
unchanged. Everything that is specific to building this outside KWin lives
outside that folder.

## Licence

`GPL-2.0-or-later`, REUSE compliant. Third-party shaders keep their own licence.
