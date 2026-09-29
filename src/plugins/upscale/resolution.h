/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KWin
{

struct UpscaleSize
{
    int width;
    int height;
    bool operator==(const UpscaleSize &) const = default;
};

// Native first, so that the value a fresh configuration reads as zero is the
// one that asks for nothing. There is no Automatic: what it used to mean was
// "nobody has chosen", which a profile now says by storing no resolution at
// all and inheriting the global one.
enum class ResolutionPreset {
    Native,
    UltraQuality,
    Quality,
    Balanced,
    Performance,
    Custom,
};

// Compare physical pixel counts, not dimensions: ultrawide and portrait
// outputs with the same workload must receive the same policy. Promote before
// multiplying so large modes cannot overflow on either supported word size.
inline bool exceedsMinimumPixels(UpscaleSize output, int minimumPixels)
{
    return output.width > 0 && output.height > 0
        && int64_t(output.width) * output.height > std::max(0, minimumPixels);
}

/**
 * The share of the destination a preset asks for, from one to a half.
 *
 * Custom's share is given in basis points, hundredths of a percent, because a
 * whole percent cannot name the shares that give the resolutions people know:
 * 2560 × 1440 on a 3840 × 2160 screen is 66.67 %, and 67 % renders
 * 2573 × 1447 instead.
 */
inline double resolutionRatio(ResolutionPreset preset, int basisPoints)
{
    switch (preset) {
    case ResolutionPreset::UltraQuality:
        return 1.0 / 1.3;
    case ResolutionPreset::Quality:
        return 1.0 / 1.5;
    case ResolutionPreset::Balanced:
        return 1.0 / 1.7;
    case ResolutionPreset::Performance:
        return 0.5;
    case ResolutionPreset::Custom:
        return std::clamp(basisPoints, 5000, 10000) / 10000.0;
    default:
        return 1.0;
    }
}

inline UpscaleSize desiredResolution(UpscaleSize output, ResolutionPreset preset, int basisPoints)
{
    const double ratio = resolutionRatio(preset, basisPoints);
    return {int(std::round(output.width * ratio)), int(std::round(output.height * ratio))};
}

/**
 * How a supplied buffer size relates to the destination it would be scaled to.
 *
 * The scaler needs one answer, but a refused buffer needs the condition that
 * actually refused it: waiting for the next commit, a game rendering at native
 * resolution and a wrong aspect ratio are three different problems.
 */
enum class UpscaleSizing {
    Supported,
    EmptyBuffer,
    NotSmaller,
    BelowHalf,
    AspectRatio,
};

inline UpscaleSizing upscaleSizing(UpscaleSize input, UpscaleSize output)
{
    if (input.width <= 0 || input.height <= 0) {
        return UpscaleSizing::EmptyBuffer;
    }
    if (output.width <= input.width || output.height <= input.height) {
        return UpscaleSizing::NotSmaller;
    }
    if (double(output.width) > 2.0 * input.width || double(output.height) > 2.0 * input.height) {
        return UpscaleSizing::BelowHalf;
    }
    // Independently rounding both dimensions can move the aspect ratio by
    // half a pixel on each axis. Use cross products, without integer overflow.
    const double difference = std::abs((double(input.width) * output.height) - (double(input.height) * output.width));
    return difference <= 0.5 * (double(output.width) + output.height) ? UpscaleSizing::Supported : UpscaleSizing::AspectRatio;
}

/**
 * Whether two edges in logical coordinates fall on the same device pixel.
 *
 * Logical geometry is fractional whenever the output's scale is. A 3840 x 2160
 * output at scale 1.45 is 2648.28 x 1489.66 logical, and a client can only ever
 * commit whole pixels, so no window can equal that rectangle exactly. Comparing
 * the two as they stand therefore refuses a window that covers the screen
 * completely.
 *
 * The question is settled where the answer is defined, in the device pixels the
 * scaler reads and writes. Two edges that round to the same pixel cover the same
 * pixel, and nothing finer than a pixel can be drawn differently.
 */
inline bool upscaleSamePixel(double first, double second, double scale)
{
    // Within one device pixel, rather than rounding each side and comparing.
    // Rounding on its own is not enough: two edges a fraction of a pixel apart
    // can still fall either side of a rounding boundary. At scale 1.45,
    // logical heights of 1490.3 and 1489.7 differ by less than one device
    // pixel but round to different integers.
    //
    // One pixel exactly is common, and the arithmetic that finds it lands a
    // hair either side of one: at scale 2.7 an output 3840 pixels wide is
    // 1422.22 logical, a fullscreen client makes its window 1422, which KWin
    // places at 3839 pixels, 1421.85 logical, and the difference comes back
    // as 1.0000000000002 (SuperTuxKart refused on Kubuntu 26.04, 2026-09-29).
    // A millionth of a pixel is room for that and nothing a screen can show.
    return std::abs(first - second) * scale <= 1.0 + 1e-6;
}

inline bool canUpscale(UpscaleSize input, UpscaleSize output)
{
    return upscaleSizing(input, output) == UpscaleSizing::Supported;
}

/**
 * The buffer a client of the scale-driven kind would commit at integer scale
 * @p scale on this output.
 *
 * Such a client renders the logical screen size multiplied by the scale it was
 * told, so telling it a smaller scale is what makes it render less.
 */
inline UpscaleSize scaledRequest(UpscaleSize outputPixels, double outputScale, int scale)
{
    if (outputScale <= 0 || scale <= 0) {
        return {0, 0};
    }
    const double ratio = scale / outputScale;
    return {int(std::round(outputPixels.width * ratio)), int(std::round(outputPixels.height * ratio))};
}

/**
 * The integer scale to tell a scale-driven client, or zero to tell it nothing.
 *
 * A wish is a ratio, but this kind of client can only be moved in whole steps
 * of the output's own scale: the Wayland output scale is an integer, so on a
 * screen at scale 2 the only reduction available is a half, and on one at
 * scale 3 it is two thirds or a third. The wish is therefore answered with the
 * reachable size closest to it rather than refused for not being reachable
 * exactly, and callers report which one was actually asked for.
 *
 * Steps that the scaler would then refuse are not offered at all, which is
 * what removes the third on a scale-3 screen: a third of the destination is
 * below the half that FSR 1 enlarges from.
 */
inline int reachableScale(UpscaleSize outputPixels, double outputScale, ResolutionPreset preset, int basisPoints)
{
    const double wanted = resolutionRatio(preset, basisPoints);
    // Native asks for the whole output, which resolutionRatio() returns as
    // one, so the bound below covers it without naming it.
    if (wanted >= 1.0) {
        return 0;
    }
    int best = 0;
    double bestDistance = 0;
    // A scale of one leaves no whole step below it, so such an output offers
    // this kind of client nothing at all. That is a property of the client,
    // not a failure: the caller advertises nothing rather than a size the
    // client would ignore, and a window still drawing at full size is then
    // asked for a fractional surface scale instead.
    for (int candidate = 1; double(candidate) < outputScale; ++candidate) {
        const UpscaleSize size = scaledRequest(outputPixels, outputScale, candidate);
        if (upscaleSizing(size, outputPixels) != UpscaleSizing::Supported) {
            continue;
        }
        const double distance = std::abs((candidate / outputScale) - wanted);
        if (best == 0 || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

inline double sharpeningAmount(bool enabled, int percentage)
{
    // AMD's zero stop parameter means maximum sharpening. A zero UI value
    // instead has a real bypass; positive values multiply the RCAS lobe.
    return enabled ? std::clamp(percentage, 0, 100) / 100.0 : 0.0;
}

} // namespace KWin
