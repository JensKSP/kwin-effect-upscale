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

inline double resolutionRatio(ResolutionPreset preset, int percentage)
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
        return std::clamp(percentage, 50, 100) / 100.0;
    default:
        return 1.0;
    }
}

inline UpscaleSize desiredResolution(UpscaleSize output, ResolutionPreset preset, int percentage)
{
    const double ratio = resolutionRatio(preset, percentage);
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
inline int reachableScale(UpscaleSize outputPixels, double outputScale, ResolutionPreset preset, int percentage)
{
    const double wanted = resolutionRatio(preset, percentage);
    // Native asks for the whole output, which resolutionRatio() returns as
    // one, so the bound below covers it without naming it.
    if (wanted >= 1.0) {
        return 0;
    }
    int best = 0;
    double bestDistance = 0;
    // A scale of one leaves no whole step below it, so such an output offers
    // this kind of client nothing at all. That is a property of the client,
    // not a failure, and the caller says so rather than asking for a size the
    // client would ignore.
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
