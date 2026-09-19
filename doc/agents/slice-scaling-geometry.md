<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: aspect ratio and integer scaling

Status: required on 2026-09-18; specified, not implemented. The permanent
[handbook](../upscaling.md#aspect-ratio-and-integer-scaling) defines the behaviour.
Keep this document until implementation and required testing, including
real-device acceptance, are complete.

## Start state

The current path requires matching aspect ratios and limits enlargement to
two times per axis. No aspect-preserving bars or integer nearest-neighbour
path has been implemented or accepted.

## End state

Fullscreen games can use aspect-preserving fit or integer nearest-neighbour
scaling with correct bars, input mapping and colour/presentation handling.
Global/profile controls report effective geometry, and the acceptance examples,
supported-container checks and real-device tests below have passed.

## Dependencies

Extend the existing renderer and global controls. Integrate sparse settings
through [profiles](slice-application-profiles.md); the [overlay](slice-game-overlay.md)
can expose the same settings when available but is not required to validate
them through the configuration module. Use verified resolution-control surface
handling for cooperative clients. Already-full-output borderless windows are
eligible today; arbitrary smaller-window fullscreen presentation
and display-specific settings remain excluded as stated below.

## Scope

Support fullscreen games with aspect ratios different from the output, and
pixel art with integer nearest-neighbour scaling. Preserve the whole image,
centre it with black bars and leave the physical output mode unchanged. Provide
global defaults and sparse application overrides, with controls and status in
the configuration module and a shared interface for the planned overlay. HDR and VRR remain requirements.

This is a required extension beyond the initial FSR path's matching-aspect and
two-times-per-axis restrictions. Broader integer factors apply to the nearest
filter; this does not relax FSR's supported range. Display-specific overrides,
fullscreen presentation of windowed games and sharpening at native resolution
remain optional later directions, outside this slice.

## Approach

1. Separate source dimensions, output dimensions and the image destination
   rectangle. Compute aspect-preserving fit and integer geometry in physical
   pixels, including one-times presentation and an explicit unavailable result
   when no positive integer factor fits.
2. Separate geometry from filter choice. Add nearest-neighbour sampling without
   sharpening for pixel replication; retain the documented FSR limits. Resolve
   supported combinations before rendering, report unavailable ones and preserve
   normal rendering on failure.
3. Integrate the new destination with KWin's rendering and input handling on both
   supported targets. Establish how fullscreen coverage, pointer coordinates,
   confinement, relative motion, popups and surface trees stay consistent;
   drawing a smaller rectangle alone is insufficient. Keep KWin unpatched.
4. Draw black bars and filter the image without sampling across its boundary.
   Reuse KWin colour management, preserve separate overlays and the cursor, and
   update damage/cleanup on geometry changes without continuous repainting.
5. Expose global/profile settings and live changes only for verified geometry
   and input paths. Report both image destination and output dimensions. Keep
   comparison mode at the same supplied input and destination geometry.
6. Add appropriate geometry, rendering, configuration and integration coverage,
   then complete real-game and TV acceptance. Preserve durable decisions in
   source comments and the handbook before deleting this slice document.

## Acceptance criteria

Planned checks, not observed results:

- Fit: 1440 × 1080 on 3840 × 2160 gives a 2880 × 2160 image and 480-pixel side
  bars. Cover wider inputs with top/bottom bars, odd dimensions, rounding,
  desktop scaling, resize and output changes; preserve the complete image.
- Integer: 320 × 240 on 3840 × 2160 uses 9×; 1280 × 720 uses 3× and fills that
  output. Cover factors of one, no fitting factor, invalid/large dimensions
  and overflow. No fractional factor or stretching may be labelled integer.
- Rendering: use pixel patterns to prove nearest-neighbour replication,
  orientation, bar placement, image-edge sampling and sharpening bypass.
  Keep supplied letterboxing unchanged. Test permitted FSR combinations and
  clear fallback for unsupported geometry/filter choices.
- Input/lifecycle: native Wayland and Xwayland, including Proton and standalone
  Wine; absolute and relative pointer movement, confinement/locking, focus,
  popups, separate overlays, deactivation, failure and restoring normal rendering.
  The output mode and unrelated windows must remain unchanged.
- Configuration and shared status: global defaults and sparse overrides, live changes,
  actual image/output dimensions, unavailable states and unchanged saved values
  after temporary comparison.
- Run repository checks in Trixie, build with GCC and Clang with warnings as
  errors in both Trixie and neon-unstable, and run relevant rendering and
  virtual-compositor tests. On wzpc, inspect retro/pixel-art and differing-aspect
  games on the TV, verify input, HDR and actual adaptive presentation, and
  measure the added processing cost.

## Progress and remaining work

- [x] Record required scope and distinguish optional later features.
- [x] Specify geometry, filter separation and acceptance examples.
- [ ] Establish supported KWin geometry/input integration.
- [ ] Implement geometry, nearest filtering, settings and status.
- [ ] Complete automated checks, both compiler/container builds and TV acceptance.

Documentation validation, 2026-09-18: `pre-commit run --all-files` and the complete
pre-push stage passed in Trixie on an isolated copy under
`build/overlay-geometry-check`, containing the committed source and updated
documents. Local documentation links and heading anchors resolved. No
implementation, rendering test or real-device acceptance was performed for
this extension.
