/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resolution.h"

#include <array>
#include <cassert>
#include <limits>

int main()
{
    using namespace KWin;
    const UpscaleSize output{3840, 2160};
    assert((desiredResolution(output, ResolutionPreset::UltraQuality, 50) == UpscaleSize{2954, 1662}));
    assert((desiredResolution(output, ResolutionPreset::Quality, 50) == UpscaleSize{2560, 1440}));
    assert((desiredResolution(output, ResolutionPreset::Balanced, 50) == UpscaleSize{2259, 1271}));
    assert((desiredResolution(output, ResolutionPreset::Performance, 99) == UpscaleSize{1920, 1080}));
    assert((desiredResolution(output, ResolutionPreset::Native, 50) == output));
    assert((desiredResolution(output, ResolutionPreset::Custom, 75) == UpscaleSize{2880, 1620}));
    assert((desiredResolution({101, 101}, ResolutionPreset::Custom, 50) == UpscaleSize{51, 51}));
    assert((desiredResolution(output, ResolutionPreset::Custom, -1) == UpscaleSize{1920, 1080}));
    assert((desiredResolution(output, ResolutionPreset::Custom, 999) == output));
    assert(canUpscale({1920, 1080}, output));
    assert(canUpscale({2560, 1440}, output));
    assert(canUpscale({2259, 1271}, output));
    assert(!canUpscale(output, output));
    assert(!canUpscale({1280, 720}, output));
    assert(!canUpscale({1919, 1080}, output));
    assert(!canUpscale({2560, 1600}, output));
    assert(!canUpscale({0, 0}, output));
    assert(!canUpscale({-1, 1080}, output));
    const int maximum = std::numeric_limits<int>::max();
    assert(canUpscale({maximum / 2 + 1, maximum / 2 + 1}, {maximum, maximum}));
    for (const UpscaleSize destination : std::array{output, UpscaleSize{2560, 1440}, UpscaleSize{3440, 1440}}) {
        for (int percentage = 50; percentage < 100; ++percentage) {
            assert(canUpscale(desiredResolution(destination, ResolutionPreset::Custom, percentage), destination));
        }
    }
    assert(sharpeningAmount(false, 100) == 0);
    assert(sharpeningAmount(true, 0) == 0);
    assert(sharpeningAmount(true, 100) == 1);
    assert(sharpeningAmount(true, 25) < sharpeningAmount(true, 75));
}
