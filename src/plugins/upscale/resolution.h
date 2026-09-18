/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <cmath>

namespace KWin
{

struct UpscaleSize
{
    int width;
    int height;
    bool operator==(const UpscaleSize &) const = default;
};

enum class ResolutionPreset {
    Automatic,
    Native,
    UltraQuality,
    Quality,
    Balanced,
    Performance,
    Custom,
};

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

inline double sharpeningAmount(bool enabled, int percentage)
{
    // AMD's zero stop parameter means maximum sharpening. A zero UI value
    // instead has a real bypass; positive values multiply the RCAS lobe.
    return enabled ? std::clamp(percentage, 0, 100) / 100.0 : 0.0;
}

} // namespace KWin
