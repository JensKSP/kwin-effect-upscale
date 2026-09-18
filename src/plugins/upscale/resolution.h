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

inline bool canUpscale(UpscaleSize input, UpscaleSize output)
{
    if (input.width <= 0 || input.height <= 0 || output.width <= input.width || output.height <= input.height
        || double(output.width) > 2.0 * input.width || double(output.height) > 2.0 * input.height) {
        return false;
    }
    // Independently rounding both dimensions can move the aspect ratio by
    // half a pixel on each axis. Use cross products, without integer overflow.
    const double difference = std::abs((double(input.width) * output.height) - (double(input.height) * output.width));
    return difference <= 0.5 * (double(output.width) + output.height);
}

inline double sharpeningAmount(bool enabled, int percentage)
{
    // AMD's zero stop parameter means maximum sharpening. A zero UI value
    // instead has a real bypass; positive values multiply the RCAS lobe.
    return enabled ? std::clamp(percentage, 0, 100) / 100.0 : 0.0;
}

} // namespace KWin
