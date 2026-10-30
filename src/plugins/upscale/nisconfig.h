/*
    SPDX-FileCopyrightText: 2022 NVIDIA CORPORATION & AFFILIATES

    SPDX-License-Identifier: MIT

    The MIT License(MIT)

    Copyright(c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy of
    this software and associated documentation files(the "Software"), to deal in
    the Software without restriction, including without limitation the rights to
    use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
    the Software, and to permit persons to whom the Software is furnished to do so,
    subject to the following conditions :

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
    FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
    COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// The part of NVScalerUpdateConfig, from the NVIDIA Image Scaling SDK v1.0.3
// (NIS/NIS_Config.h at commit 35e13ba3), that shapes NVScaler's filter: its
// edge detection and its sharpening as functions of the sharpness and the HDR
// mode. The viewport and scale fields are left out; the fragment program
// derives its position from its texture coordinate.

#pragma once

#include <algorithm>

namespace KWin
{

/** NISHDRMode: how the values NIS filters are encoded. */
enum class UpscaleNisMode {
    // Display-referred and encoded, zero to one: sRGB, gamma 2.2.
    Sdr,
    // Linear, 1.0 being 80 nits.
    Linear,
    // PQ, zero to one.
    Pq,
};

struct UpscaleNisConfig
{
    float detectRatio = 0;
    float detectThreshold = 0;
    float minContrastRatio = 0;
    float ratioNorm = 0;
    float sharpStartY = 0;
    float sharpScaleY = 0;
    float sharpStrengthMin = 0;
    float sharpStrengthScale = 0;
    float sharpLimitMin = 0;
    float sharpLimitScale = 0;
};

/**
 * NVScaler's configuration for @p sharpness, zero to one, in @p mode.
 *
 * NVIDIA maps a sharpness of zero to an unsharp mask scaled by at most 0.025,
 * in the darkest values, rather than to none. The effect's zero is a bypass
 * like every other filter's, so there the mask's strength is zero outright.
 */
inline UpscaleNisConfig upscaleNisConfig(float sharpness, UpscaleNisMode mode)
{
    // NVIDIA's names, in KDE's spelling: sharpen_slider is sharpenSlider and
    // MaxScale maxScale, and the k-prefixed constants keep theirs. The
    // arithmetic is NVIDIA's, with its precedence written out.

    // adjust params based on value from sharpness slider
    sharpness = std::max<float>(std::min<float>(1.f, sharpness), 0.f);
    const float sharpenSlider = sharpness - 0.5f; // Map 0 to 1 to -0.5 to +0.5

    // Different range for 0 to 50% vs 50% to 100%
    // The idea is to make sure sharpness of 0% map to no-sharpening,
    // while also ensuring that sharpness of 100% doesn't cause too much over-sharpening.
    const float maxScale = (sharpenSlider >= 0.0f) ? 1.25f : 1.75f;
    const float minScale = (sharpenSlider >= 0.0f) ? 1.25f : 1.0f;
    const float limitScale = (sharpenSlider >= 0.0f) ? 1.25f : 1.0f;

    const float kDetectRatio = 2 * 1127.f / 1024.f;

    // Params for SDR
    float kDetectThres = 64.0f / 1024.0f;
    float kMinContrastRatio = 2.0f;
    float kMaxContrastRatio = 10.0f;

    float kSharpStartY = 0.45f;
    float kSharpEndY = 0.9f;
    float kSharpStrengthMin = std::max<float>(0.0f, 0.4f + (sharpenSlider * minScale * 1.2f));
    float kSharpStrengthMax = 1.6f + (sharpenSlider * maxScale * 1.8f);
    float kSharpLimitMin = std::max<float>(0.1f, 0.14f + (sharpenSlider * limitScale * 0.32f));
    float kSharpLimitMax = 0.5f + (sharpenSlider * limitScale * 0.6f);

    if (mode == UpscaleNisMode::Linear || mode == UpscaleNisMode::Pq) {
        kDetectThres = 32.0f / 1024.0f;

        kMinContrastRatio = 1.5f;
        kMaxContrastRatio = 5.0f;

        kSharpStrengthMin = std::max<float>(0.0f, 0.4f + (sharpenSlider * minScale * 1.1f));
        kSharpStrengthMax = 2.2f + (sharpenSlider * maxScale * 1.8f);
        kSharpLimitMin = std::max<float>(0.06f, 0.10f + (sharpenSlider * limitScale * 0.28f));
        kSharpLimitMax = 0.6f + (sharpenSlider * limitScale * 0.6f);

        if (mode == UpscaleNisMode::Pq) {
            kSharpStartY = 0.35f;
            kSharpEndY = 0.55f;
        } else {
            kSharpStartY = 0.3f;
            kSharpEndY = 0.5f;
        }
    }

    UpscaleNisConfig config;
    config.detectRatio = kDetectRatio;
    config.detectThreshold = kDetectThres;
    config.minContrastRatio = kMinContrastRatio;
    config.ratioNorm = 1.0f / (kMaxContrastRatio - kMinContrastRatio);
    config.sharpStartY = kSharpStartY;
    config.sharpScaleY = 1.0f / (kSharpEndY - kSharpStartY);
    config.sharpStrengthMin = kSharpStrengthMin;
    config.sharpStrengthScale = kSharpStrengthMax - kSharpStrengthMin;
    config.sharpLimitMin = kSharpLimitMin;
    config.sharpLimitScale = kSharpLimitMax - kSharpLimitMin;
    if (sharpness <= 0.0f) {
        config.sharpStrengthMin = 0.0f;
        config.sharpStrengthScale = 0.0f;
    }
    return config;
}

} // namespace KWin
