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
    // Every refusal names one condition, because a report that cannot tell
    // "the game already renders at native resolution" from "its aspect ratio
    // does not match the output" leaves the user with nothing to change.
    assert(upscaleSizing({1920, 1080}, output) == UpscaleSizing::Supported);
    assert(upscaleSizing(output, output) == UpscaleSizing::NotSmaller);
    assert(upscaleSizing({4000, 1080}, output) == UpscaleSizing::NotSmaller);
    // One pixel below half the destination width is already too small, and
    // the size relation is checked before the aspect ratio it also fails.
    assert(upscaleSizing({1919, 1080}, output) == UpscaleSizing::BelowHalf);
    assert(upscaleSizing({1280, 720}, output) == UpscaleSizing::BelowHalf);
    assert(upscaleSizing({2560, 1600}, output) == UpscaleSizing::AspectRatio);
    assert(upscaleSizing({0, 0}, output) == UpscaleSizing::EmptyBuffer);
    assert(upscaleSizing({-1, 1080}, output) == UpscaleSizing::EmptyBuffer);
    // An empty buffer is reported as such rather than as the size relation it
    // would also fail, so waiting for a first commit stays distinguishable.
    assert(upscaleSizing({0, 0}, {0, 0}) == UpscaleSizing::EmptyBuffer);
    const int maximum = std::numeric_limits<int>::max();
    assert(canUpscale({maximum / 2 + 1, maximum / 2 + 1}, {maximum, maximum}));
    for (const UpscaleSize destination : std::array{output, UpscaleSize{2560, 1440}, UpscaleSize{3440, 1440}}) {
        for (int percentage = 50; percentage < 100; ++percentage) {
            assert(canUpscale(desiredResolution(destination, ResolutionPreset::Custom, percentage), destination));
        }
    }
    // A client whose buffer is the logical screen times an integer scale can
    // only be moved in whole steps, so a wish is answered with the reachable
    // size nearest to it. An upright 4K screen at scale 1 offers no step at
    // all; at scale 2 the only one is a half; at scale 3 a third is below what
    // FSR 1 enlarges from, which leaves two thirds.
    assert(reachableScale(output, 1, ResolutionPreset::Performance, 50) == 0);
    assert(reachableScale(output, 2, ResolutionPreset::Performance, 50) == 1);
    assert((scaledRequest(output, 2, 1) == UpscaleSize{1920, 1080}));
    assert(reachableScale(output, 3, ResolutionPreset::Performance, 50) == 2);
    assert((scaledRequest(output, 3, 2) == UpscaleSize{2560, 1440}));
    assert((scaledRequest(output, 3, 1) == UpscaleSize{1280, 720}));
    assert(upscaleSizing(scaledRequest(output, 3, 1), output) == UpscaleSizing::BelowHalf);
    // A quality wish on a screen that can only halve is answered with the half
    // rather than refused, and the caller reports what was asked for.
    assert(reachableScale(output, 2, ResolutionPreset::Quality, 50) == 1);
    // Asking for no reduction asks for no scale.
    assert(reachableScale(output, 2, ResolutionPreset::Automatic, 50) == 0);
    assert(reachableScale(output, 2, ResolutionPreset::Native, 50) == 0);
    assert(reachableScale(output, 2, ResolutionPreset::Custom, 100) == 0);
    // A fractional desktop scale still offers whole steps below it.
    assert(reachableScale(output, 1.5, ResolutionPreset::Quality, 50) == 1);
    assert((scaledRequest(output, 1.5, 1) == UpscaleSize{2560, 1440}));
    assert(scaledRequest(output, 0, 1).width == 0);
    assert(scaledRequest(output, 2, 0).width == 0);
    // Whatever is reachable must be something the scaler then accepts.
    for (const double scale : {1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0}) {
        for (const UpscaleSize destination : std::array{output, UpscaleSize{2560, 1440}, UpscaleSize{1920, 1080}}) {
            const int step = reachableScale(destination, scale, ResolutionPreset::Performance, 50);
            assert(step == 0 || canUpscale(scaledRequest(destination, scale, step), destination));
        }
    }
    assert(sharpeningAmount(false, 100) == 0);
    assert(sharpeningAmount(true, 0) == 0);
    assert(sharpeningAmount(true, 100) == 1);
    assert(sharpeningAmount(true, 25) < sharpeningAmount(true, 75));
}
