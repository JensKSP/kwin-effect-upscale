<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Upscaling fullscreen windows

What this effect is for, what it deliberately leaves alone, and which questions
about KWin are still open. Statements about KWin were read in the source at
`v6.3.6` (commit `b8de432`, the version Debian Trixie ships as `4:6.3.6-1`) and
at `master` (commit `c1ca390`, 2026-09-13).

## The gap

A game that renders below the resolution of the screen it covers has to be
enlarged by someone. On Windows that someone is the graphics driver: AMD calls
it RSR, NVIDIA calls it NIS, and the compositor is not involved. On Linux
`amdgpu` and Mesa have no equivalent. What is left is:

| Where | What happens |
| --- | --- |
| gamescope | It is a compositor of its own. The game gets a virtual screen of the inner size (`-w/-h`) and the finished image is scaled to the output (`-W/-H`) with FSR 1, NIS, bilinear or integer scaling. |
| Proton-GE | `WINE_FULLSCREEN_FSR=1` offers smaller resolutions to the game and scales them with FSR 1. |
| A plain KDE session | Nothing. Steam is an ordinary window, and the image is stretched with whatever filtering the compositor happens to use. |

In a plain session that filtering is bilinear (`GL_LINEAR`, e.g.
`src/opengl/glframebuffer.h`) or the hardware plane doing it (`SRC_W`/`CRTC_W`,
`src/backends/drm/drm_pipeline.cpp:265-274`). Neither KWin 6.3.6 nor master
carries FSR or NIS.

## What this effect does

It takes the other route from gamescope: **the game picks its resolution and
reports it honestly; the compositor decides only how the image is enlarged.**

The effect acts on a window when all of this holds:

- the window is fullscreen on its output, and
- its buffer is smaller than the output area it covers, and
- the effect is enabled and a scaler is configured.

Everything else is left alone. There is no virtual screen, the reported screen
size is never faked, and a window that renders at native size is not touched.

## What it does not do

- **No frame generation.** Frames are scaled, never invented.
- **No temporal upscaling.** FSR 2/3/4, XeSS, DLSS, Arm ASR and SGSR 2 need
  motion vectors from inside the game. A compositor does not have them, and
  faking them would produce artefacts the game cannot correct for.
- **No replacement for in-game upscalers.** A game that ships FSR 2 or DLSS
  should use it; it has data this effect will never see.
- **No resolution spoofing.** Making a game believe the screen is smaller is
  gamescope's design, not this one.

## Scalers

Only spatial scalers can be used, for the reason above. Candidates, with the
licence that decides whether the shader may be carried here at all:

| Scaler | Kind | Licence | Usable |
| --- | --- | --- | --- |
| AMD FSR 1 (EASU + RCAS) | upscale + sharpen | MIT | yes |
| NVIDIA Image Scaling (NIS) | upscale + sharpen | MIT | yes |
| Snapdragon GSR 1 | single pass, Lanczos-like 12-tap + adaptive sharpen | BSD-3-Clause | yes |
| Lanczos, bicubic, integer, xBRZ | classic / pixel art | free | yes |
| AMD CAS | sharpen only | MIT (not verified) | as an addition |
| MAKO Scaler | single pass | GPL-3.0-or-later | **no** |

MAKO is ruled out as a source of code, not on quality: this project is
`GPL-2.0-or-later`, and taking GPL-3 code would pin it to GPL-3 and rule out a
merge into KWin, whose effects are `GPL-2.0-or-later`. Third-party shaders that
are used keep their own licence headers; see `LICENSES/`.

For reference, KWin master already carries an xBRZ upscaler in the zoom effect
(`src/plugins/zoom/shaders/upscaler.frag`).

## Where the effect has to hook into KWin

- **Getting the surface at buffer size.** `EffectWindow::windowItem()`
  (`src/effect/effectwindow.h:672`) leads to `SurfaceItem`, which exposes
  `bufferSize()`, `bufferSourceBox()` and `destinationSize()`
  (`src/scene/surfaceitem.h:29-38`). The usual offscreen route hands the window
  over at target size, which is one scaling step too late.
- **Preventing direct scanout**, or the compositor hands the buffer to the
  display hardware and never calls the effect at all:
  `Effect::blocksDirectScanout()` (`src/effect/effect.h:902`).
- **Wayland clients that scale themselves** through `wp_viewporter`
  (`src/wayland/surface.cpp`, `viewport.sourceGeometry`, `destinationSize`)
  must not be scaled twice.

## Open questions

1. Can an effect in 6.3.6 reach the surface texture at **buffer size**, or does
   the offscreen path always deliver it at target size?
2. How does blocking direct scanout interact with tearing, HDR and adaptive
   sync? Blocking it unconditionally would cost more than the scaling gains.
3. Xwayland games — the Proton default — on 6.3.6, which lacks master's RandR
   resolution emulation (`_XWAYLAND_RANDR_EMU_MONITOR_RECTS`,
   `src/x11window.cpp:3469-3474`): does a 1080p fullscreen window arrive as a
   small buffer with a 4K target size?
4. Does KWin reject a plugin built against a different effect API version
   cleanly? 6.3.6 declares version `0.236` (`src/effect/effect.h:101-104`); the
   place where it is checked at load time has not been found yet.

Question 1 decides whether the effect is possible at all in the supported
version. Question 4 decides how packaging has to deal with KWin updates.

## Versions

| | Version | Role |
| --- | --- | --- |
| Minimum | KWin 6.3.6, effect API `0.236` | what Debian Trixie ships; the supported target |
| Tracked | KWin git master | built in CI to catch API changes early, not a supported target |

Master renames some things the effect will touch, `QRect` becoming KWin's own
`Rect` among them. Where that matters, the difference is absorbed in a thin
compatibility layer rather than in the effect's logic.

## Configuration

Planned, none of it implemented yet: enabling the effect, choosing the scaler,
and a sharpening strength for the scalers that have one. It follows KWin's own
pattern — `upscaleconfig.kcfg` plus a configuration page registered as
`X-KDE-ConfigModule` — so that it appears in System Settings like any other
effect.
