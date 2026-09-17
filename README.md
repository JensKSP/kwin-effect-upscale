<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# kwin-effect-upscale

A KWin effect that upscales fullscreen windows rendering below the resolution
of the output they cover.

On Linux there is no driver-side equivalent of AMD RSR or NVIDIA NIS, so a game
that renders at 1080p on a 4K screen is enlarged by whatever the compositor
happens to do, which today means bilinear filtering. gamescope solves this by
being a compositor of its own and handing the game a virtual screen. This effect
takes the other route: the game picks its resolution and reports it honestly,
and KWin decides only *how* the image is enlarged.

What it does, what it deliberately leaves alone, which scalers can be used and
what is still unanswered about KWin is written down in
[doc/upscaling.md](doc/upscaling.md).

This is not an official KDE project.

## State

Skeleton. The effect builds, loads and does nothing yet.

## Building

Against Debian Trixie (KWin 6.3.6):

```bash
podman build -t kwin-effect-upscale:trixie containers/trixie
podman run --rm -v "$PWD":/src:Z kwin-effect-upscale:trixie \
    bash -c 'cmake -B build -S . && cmake --build build --parallel'
```

Packagers turn the strict warnings off with
`-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF`.

Configuring on a machine that has the dependencies also installs a pre-commit
hook (ECM's `KDEGitCommitHooks`) checking formatting, the plugin metadata and
the repository rules. CI runs the same checks, because a hook can be skipped.

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
