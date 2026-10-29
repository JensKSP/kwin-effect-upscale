<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Slice: Scaling methods

## Status

Opened on 2026-10-29 on Jens's instruction to offer the other scaling methods
as options, so that a user can choose a method and its sharpening globally and
for each game. He decided the scope the same day: the classic filters
(bilinear, bicubic, Lanczos), Snapdragon GSR 1, NVIDIA Image Scaling and xBRZ
join FSR 1 and nearest neighbour. Each method sharpens with its own sharpener
under the existing sharpening switch and strength. The work goes on branch
`scaling/methods`. Bilinear, bicubic and Lanczos are implemented with their
settings, status and rendered-pixel tests; GSR, NIS and xBRZ follow. See
[progress](#progress-and-remaining-work).

## Start state and evidence

- **Two filters.** `Filter` (`UpscaleSetting::Filter`, `upscaleconfig.kcfg`
  0 to 1, written `FSR` or `Nearest` in `kwinupscalerc`) chooses FSR 1 or
  nearest neighbour, globally and as a sparse per-game override like every
  other preference (`settings.h`).
- **Sharpening drives RCAS alone.** `Sharpening` and `Strength` (0 to 100 %)
  are global and per game. `UpscaleSettings::sharpening()` gives the RCAS
  strength and zero for nearest neighbour, which replicates pixels exactly.
- **The scaler** (`scaler.cpp`) captures the surface at buffer size in the
  destination's encoding: `GL_RGB10_A2` filtered directly for sRGB, gamma 2.2
  and PQ, `GL_RGBA32F` folded into the bounded working encoding
  (`workingcolor.glsl`) for a linear destination. EASU draws to the destination,
  or to an intermediate that RCAS sharpens. Nearest is a plain copy.
- **The picture rules** (`picture.h`) give FSR more than one and at most two
  times, and under Integer exactly two; nearest any factor, one included.
- The handbook's [scaler table](../upscaling.md#scalers) lists the other
  methods as usable or as candidates, with their licences.

## End state

A user chooses one of eight methods on the settings page's Scaling tab, as the
global default and for any game. The sharpening switch and strength apply to
each method through its own sharpener. The status, the heads-up display and
the settings page name the method that drew the picture. A combination a method
cannot honour is refused by name and the window is drawn by KWin.

| Method | Source and licence | Enlarges | Sharpened by | Filter domain |
| --- | --- | --- | --- | --- |
| FSR 1 | AMD reference, MIT (implemented) | more than 1 and at most 2 times; Integer 2 | RCAS after EASU | as implemented |
| Nearest neighbor | own (implemented) | any factor, a whole one of one included | never | copy |
| Bilinear | own, GPL-2.0-or-later | any factor above 1 | RCAS after it | as EASU |
| Bicubic | own, GPL-2.0-or-later | any factor above 1 | RCAS after it | as EASU |
| Lanczos | own, GPL-2.0-or-later | any factor above 1 | RCAS after it | as EASU |
| Snapdragon GSR 1 | Qualcomm reference, BSD-3-Clause | any factor above 1 | its edge sharpness | as EASU, shifted as RCAS is |
| NVIDIA Image Scaling | NVIDIA reference, MIT | more than 1 and at most 2 times per axis; Integer 2 | its own sharpness | its SDR, PQ and linear modes |
| xBRZ | KWin's zoom effect, GPL-2.0-or-later | any factor above 1 | never | decoded for its colour distances |

- **Sharpening.** With the switch off or the strength at zero, a method draws
  unsharpened, and every method with a separate pass skips it, as RCAS does
  today. Otherwise the strength maps onto each sharpener's own range: the RCAS
  lobe as today, GSR's `EdgeSharpness` from 1 (its unsharpened filter) to 2
  (Qualcomm's default), NIS's sharpness from 0 (NVIDIA: "no-sharpening") to 1.
  Nearest and xBRZ never sharpen. The filter's tooltip says so, and the
  sharpening controls stay editable: every value on the global page is a
  default that a game choosing another method takes, which is why that page
  greys nothing out (`UpscaleEffectConfig::updatePreview()`).
- **Integer.** Each method takes the whole factors its range allows. A method
  with an unlimited range takes a whole factor of one as nearest does today,
  and draws that copy unfiltered and unsharpened. The status says so instead of
  naming a filter that did nothing.
- **Storage.** The new values are appended to `UpscaleFilter`, so a stored `0`
  or `1` keeps its meaning. `kwinupscalerc` spells them `Bilinear`, `Bicubic`,
  `Lanczos`, `GSR`, `NIS` and `xBRZ`.

## Supported scope and full acceptance

**Supported scope**, which this slice can close and a release can ship against:
every method draws in the containers' builds against KWin 6.3.6 (Trixie) and
KWin master (neon), with GCC and with Clang, and passes its rendered-pixel tests
on llvmpipe: a constant colour comes through unchanged in each destination
encoding, a pattern lands in its place and orientation, the clipped and
scissored regions and the bars stay untouched, and sharpening rises with its
strength and is a true bypass at zero. The picture rules refuse what a method
cannot do. The settings round trip and the per-game override hold for every
method, and the status names each. The conformance machine's production run
covers one session per kind of pass: classic, GSR, NIS and xBRZ.

**Full acceptance**, which keeps the requirement open until it is met: on wzpc
with Jens at the TV, the methods are compared on identical input
(SuperTuxKart, a pixel-art game for xBRZ, text and a HUD), on HDR PQ and scRGB
outputs and with VRR, and the cost of each method is measured in the A0/A1 form
of the [rendering slice](slice-fsr1-hdr-vrr.md#required-acceptance-matrix).
A method whose HDR picture fails there stays offered only for SDR, and the
status says so.

## Scope and exclusions

In scope: the six new methods, their settings, texts in every shipped language,
status and tests, and the handbook's description of what each method does.

Excluded, each with its reason:

- **A sharpener chosen separately** from the method, such as GSR followed by
  RCAS as gamescope runs it. Jens chose each method's own sharpener.
- **Sharpening at native size** and **supersampling**: the handbook's proposed
  [processing modes](../upscaling.md#processing-modes), each a slice of its own.
- **AMD CAS**: RCAS is the implemented sharpener, and CAS expects linear input.
- **Compute shaders.** KWin's shader abstraction compiles vertex and fragment
  programs, and OpenGL ES 3.0 has no compute stage. NIS is ported to a
  fragment program.
- **Gamescope's modified SGSR**, NIS's NV12 input and its viewport support.

## Dependencies

The colour path, geometry and HDR/VRR acceptance procedure belong to the
[rendering slice](slice-fsr1-hdr-vrr.md); this slice reuses them and adds no
second colour conversion. Obtaining a smaller buffer belongs to
[resolution control](slice-resolution-control.md). The hardware runs need
Jens and the TV, and never run at the same time as container checks.

## Approach

1. **Settings and plumbing.** Extend `UpscaleFilter`, its stored names, the
   `kcfg` bounds, labels and tooltips, and the picture rules for each range.
   Map the sharpening strength for each sharpener. Name the method in the
   status and the heads-up display, with translations.
2. **Classic filters.** One fragment program, its kernel chosen when it is
   compiled: bilinear over two by two texels, Catmull-Rom bicubic over four by
   four, three-lobe Lanczos over six by six. Each fetches texels as EASU does
   and filters inside the picture's own rectangle. Lanczos and bicubic
   overshoot is limited to the bounded working range, which a linear destination
   needs in order to decode at all.
3. **Snapdragon GSR 1.** Qualcomm's single-pass program keeps its licence
   header. Its `textureGather` and its bilinear base tap become texel fetches,
   because the capture is sampled nearest and OpenGL ES 3.0 has no gather. Its
   absolute thresholds, `8/255` for an edge and `23/255` for the largest
   correction, assume a signal that fills zero to one. In the working encoding
   they run in the `2 * value - 1` domain that RCAS uses, for the same reason.
4. **NVIDIA Image Scaling.** `NVScaler` keeps NVIDIA's licence header and
   becomes a fragment program. Each output pixel reads its own six by six luma
   neighbourhood, where the compute shader shared a tile, and derives its four
   edge maps from it. The two coefficient tables, 64 phases of six taps each,
   are loaded as constants. `NVScalerUpdateConfig` becomes a C++ function the
   tests can call. The destination chooses NIS's HDR mode: none for sRGB and
   gamma 2.2, PQ for PQ, and linear for a linear destination, decoded to
   NVIDIA's 80-nit unit.
5. **xBRZ.** KWin's zoom shader keeps its author's header. Its colour
   management, written against KWin master's `colormanagement.glsl`, is
   replaced by the effect's own decode, because KWin 6.3.6 has no such include.
6. **Lifecycle.** A method's programs are compiled when it is first used, so a
   session that never chooses NIS pays nothing for it. A program that fails to
   compile refuses that method by name and draws with KWin, as an unsupported
   colour description does now.
7. The handbook's scaler section, the settings page's tooltips and the
   [third-party notices](../third-party-notices.md) describe what was built.

## Progress and remaining work

- [x] Settings, picture rules, sharpening map, status and translations, for
  the filters implemented so far; each later filter adds its own.
- [x] Bilinear, bicubic and Lanczos, with rendered-pixel tests
  (`render_filter_test.cpp`: a constant colour in all four destination
  encodings with and without RCAS, the corners at a whole and a fractional
  ratio, every pixel against the kernel computed in double precision, a ramp
  reproduced by bilinear and Catmull-Rom, an edge held within its two levels
  and drawn steeper than bilinear, RCAS and clipping). Passed in the Trixie
  container with GCC on OpenGL and OpenGL ES on 2026-10-29.
- [ ] Snapdragon GSR 1, with rendered-pixel tests.
- [ ] NVIDIA Image Scaling, with rendered-pixel tests.
- [ ] xBRZ, with rendered-pixel tests.
- [ ] Builds in both containers with GCC and Clang, both lint stages, tidy.
- [ ] Production run in the conformance machine, one session per kind of pass.
- [ ] Handbook, notices and README updated.
- [ ] Full acceptance on wzpc with Jens: image comparison, HDR, VRR, cost.

## Sources

Reviewed on 2026-10-29; citations below name these versions.

- Snapdragon GSR, `sgsr/v1/include/glsl/sgsr1_shader_mobile.frag`, commit
  `d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e`
  (<https://github.com/SnapdragonStudios/snapdragon-gsr>).
- NVIDIA Image Scaling, `NIS/NIS_Scaler.h` and `NIS/NIS_Config.h`, commit
  `35e13ba316c98eeecf16f37eae70ce88019911f6`
  (<https://github.com/NVIDIAGameWorks/NVIDIAImageScaling>). The 1 to 2 times
  range is `NVScalerUpdateConfig`'s check on `kScaleX` and `kScaleY`.
- KWin, `src/plugins/zoom/shaders/upscaler.frag`, commit
  `5b9013c50870c699ee29525e5349c7c1a1a95522`
  (<https://invent.kde.org/plasma/kwin>).
