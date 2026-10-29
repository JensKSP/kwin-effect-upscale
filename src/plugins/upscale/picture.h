/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "resolution.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KWin
{

/**
 * How the enlarged picture is laid on its output.
 *
 * Fit enlarges the whole picture as far as the filter allows without stretching
 * or cropping it, and Integer by the largest whole factor that fits, for pixel
 * art and older games. Both centre it and leave black bars where its aspect
 * ratio or the factor does not fill the output.
 */
enum class UpscaleGeometry {
    Fit,
    Integer,
};

/**
 * How the picture is sampled. Separate from the geometry: nearest sampling
 * alone is no integer scaling, and FSR can be laid either way within its range.
 *
 * Stored by number, so a new method goes at the end.
 */
enum class UpscaleFilter {
    Fsr,
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos,
    Gsr,
    Nis,
};

/** The last filter, for the bounds of a stored one. */
inline constexpr UpscaleFilter upscaleLastFilter = UpscaleFilter::Nis;

/**
 * Whether @p filter enlarges by more than one and at most two times.
 *
 * FSR documents a range of up to four times the input's pixels, and NIS's
 * configuration refuses a scale outside half to one per axis. The other
 * filters interpolate at any position and enlarge by any amount.
 */
inline bool upscaleFilterAtMostTwice(UpscaleFilter filter)
{
    return filter == UpscaleFilter::Fsr || filter == UpscaleFilter::Nis;
}

/**
 * Whether @p filter has a sharpener. Nearest neighbour replicates each pixel
 * exactly; sharpening it would only undo what it is chosen for.
 */
inline bool upscaleFilterSharpens(UpscaleFilter filter)
{
    return filter != UpscaleFilter::Nearest;
}

/**
 * Whether @p filter's sharpening is RCAS, run after it on what it drew. GSR
 * and NIS sharpen as part of their own filter.
 */
inline bool upscaleFilterSharpensWithRcas(UpscaleFilter filter)
{
    return upscaleFilterSharpens(filter) && filter != UpscaleFilter::Gsr && filter != UpscaleFilter::Nis;
}

/** Where the picture goes on its output, or why it cannot go anywhere. */
struct UpscalePicture
{
    UpscaleSizing sizing = UpscaleSizing::EmptyBuffer;
    // In device pixels from the output's top left, and only for Supported.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    // Integer's whole factor; zero for Fit.
    int factor = 0;

    bool operator==(const UpscalePicture &) const = default;
};

// Integer's size, before it is centred: the largest whole factor that fits.
inline UpscalePicture upscaleWholePicture(UpscaleSize input, UpscaleSize output, UpscaleFilter filter)
{
    const int factor = std::min(output.width / input.width, output.height / input.height);
    if (factor < 1) {
        return {.sizing = UpscaleSizing::NoWholeFactor};
    }
    if (factor == 1 && input == output) {
        return {.sizing = UpscaleSizing::NotSmaller};
    }
    if (upscaleFilterAtMostTwice(filter) && factor != 2) {
        return {.sizing = UpscaleSizing::FilterRange, .factor = factor};
    }
    return {.sizing = UpscaleSizing::Supported, .width = input.width * factor, .height = input.height * factor, .factor = factor};
}

// Fit's size, before it is centred. The axis that fills first decides, and
// the other follows it; a picture of the output's own aspect ratio, within the
// rounding a client's whole pixels allow, fills both.
inline UpscalePicture upscaleFittedPicture(UpscaleSize input, UpscaleSize output, UpscaleFilter filter)
{
    const bool wider = int64_t(input.width) * output.height >= int64_t(input.height) * output.width;
    const double scale = wider ? double(output.width) / input.width : double(output.height) / input.height;
    if (scale <= 1.0) {
        return {.sizing = UpscaleSizing::NotSmaller};
    }
    if (upscaleFilterAtMostTwice(filter) && scale > 2.0) {
        return {.sizing = UpscaleSizing::BelowHalf};
    }
    if (upscaleSameAspect(input, output)) {
        return {.sizing = UpscaleSizing::Supported, .width = output.width, .height = output.height};
    }
    return {.sizing = UpscaleSizing::Supported,
            .width = wider ? output.width : int(std::lround(input.width * scale)),
            .height = wider ? int(std::lround(input.height * scale)) : output.height};
}

/**
 * Where a supplied @p input goes on an @p output of that many device pixels.
 *
 * FSR and NIS enlarge by more than one and at most two on either axis; the
 * others by any amount above one, and Integer by one as well, which centres a
 * smaller picture without enlarging it. What cannot be honoured says why: a
 * picture no smaller than its output, one FSR or NIS would have to enlarge
 * more than twice, one no whole factor fits, and FSR or NIS asked for a whole
 * factor other than two.
 * The one pixel two bars can differ by goes to the bottom and the right.
 */
inline UpscalePicture upscalePicture(UpscaleSize input, UpscaleSize output, UpscaleGeometry geometry, UpscaleFilter filter)
{
    if (input.width <= 0 || input.height <= 0 || output.width <= 0 || output.height <= 0) {
        return {};
    }
    UpscalePicture picture = geometry == UpscaleGeometry::Integer ? upscaleWholePicture(input, output, filter)
                                                                  : upscaleFittedPicture(input, output, filter);
    if (picture.sizing == UpscaleSizing::Supported) {
        picture.x = (output.width - picture.width) / 2;
        picture.y = (output.height - picture.height) / 2;
    }
    return picture;
}

/**
 * The filter that draws @p picture when @p filter was chosen.
 *
 * A whole factor of one enlarges nothing, and every filter that takes it
 * would only interpolate at the pixels' own centres or sharpen a picture at
 * its own size; that is a copy, and drawn as one, so the status names what
 * ran rather than a filter that did nothing.
 */
inline UpscaleFilter upscaleDrawnFilter(UpscaleFilter filter, const UpscalePicture &picture)
{
    return picture.factor == 1 ? UpscaleFilter::Nearest : filter;
}

/** The @p sharpening a @p drawn filter applies: none where it has no sharpener. */
inline double upscaleDrawnSharpening(UpscaleFilter drawn, double sharpening)
{
    return upscaleFilterSharpens(drawn) ? sharpening : 0.0;
}

/**
 * The whole factor Integer asks a client's buffer to be enlarged by, for a
 * resolution that asks for the share @p ratio of its output.
 *
 * Integer enlarges by whole factors only, so the share a resolution names
 * would be told and then refused, or shown small between bars: on pcjensd,
 * 2026-10-11, Quality told a game 2560 × 1440 on 3840 × 2160, and FSR, which
 * enlarges by two and nothing else under Integer, refused it and left the game
 * in the corner. The factor is the one whose share, its reciprocal, lies
 * nearest the resolution's: Quality, Balanced and Performance ask for half the
 * output, Ultra Quality for all of it, which is asking nothing. A tie goes to
 * the larger factor, since a resolution below native asked for less. Decided
 * by Jens on 2026-10-13.
 */
inline int upscaleWholeFactor(double ratio)
{
    if (!(ratio > 0.0) || ratio >= 1.0) {
        return 1;
    }
    // The factor whose share is at or above the wish, and the next one below.
    const int above = int(1.0 / ratio);
    return 1.0 / above - ratio >= ratio - 1.0 / (above + 1) ? above + 1 : above;
}

/**
 * The buffer a client is asked to commit on an @p output of that many device
 * pixels, when its resolution asks for the share @p ratio.
 *
 * Integer divides rather than rounds: half an odd output rounded up would be
 * enlarged by two past the output, which leaves one as the only whole factor.
 * A factor of one asks for the output itself, which is asking nothing.
 */
inline UpscaleSize upscaleRequestSize(UpscaleSize output, double ratio, UpscaleGeometry geometry)
{
    if (geometry == UpscaleGeometry::Integer) {
        const int factor = upscaleWholeFactor(ratio);
        return {output.width / factor, output.height / factor};
    }
    return {int(std::round(output.width * ratio)), int(std::round(output.height * ratio))};
}

/**
 * reachableScale() for @p geometry. Under Integer the step has to give a buffer
 * Integer enlarges by the whole factor the resolution names, and the one
 * nearest that factor's share is taken; where no step does, nothing is told,
 * as for a wish no step reaches.
 */
inline int upscaleReachableScale(UpscaleSize outputPixels, double outputScale, ResolutionPreset preset, int basisPoints,
                                 UpscaleGeometry geometry)
{
    if (geometry != UpscaleGeometry::Integer) {
        return reachableScale(outputPixels, outputScale, preset, basisPoints);
    }
    const int factor = upscaleWholeFactor(resolutionRatio(preset, basisPoints));
    int best = 0;
    double bestDistance = 0;
    for (int candidate = 1; factor > 1 && double(candidate) < outputScale; ++candidate) {
        const UpscaleSize size = scaledRequest(outputPixels, outputScale, candidate);
        if (size.width <= 0 || size.height <= 0 || std::min(outputPixels.width / size.width, outputPixels.height / size.height) != factor) {
            continue;
        }
        const double distance = std::abs((candidate / outputScale) - (1.0 / factor));
        if (best == 0 || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

/**
 * nearestListedSize() for @p geometry. Under Integer the wish is the only
 * listed mode that will do: the nearest other one the scaler takes lies
 * between half the output and all of it, which Integer enlarges by one, the
 * factor FSR refuses and that leaves the picture as small as it was asked.
 */
inline UpscaleSize upscaleNearestListedSize(const std::vector<UpscaleSize> &listed, UpscaleSize wanted, UpscaleSize outputPixels,
                                            UpscaleGeometry geometry)
{
    if (geometry != UpscaleGeometry::Integer) {
        return nearestListedSize(listed, wanted, outputPixels);
    }
    return std::ranges::find(listed, wanted) != listed.end() ? wanted : UpscaleSize{0, 0};
}

} // namespace KWin
