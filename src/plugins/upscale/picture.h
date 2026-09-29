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
 */
enum class UpscaleFilter {
    Fsr,
    Nearest,
};

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
    if (filter == UpscaleFilter::Fsr && factor != 2) {
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
    if (filter == UpscaleFilter::Fsr && scale > 2.0) {
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
 * FSR enlarges by more than one and at most two on either axis; Nearest by any
 * amount above one, and Integer by one as well, which centres a smaller
 * picture without enlarging it. What cannot be honoured says why: a picture
 * no smaller than its output, one FSR would have to enlarge more than twice,
 * one no whole factor fits, and FSR asked for a whole factor other than two.
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

} // namespace KWin
