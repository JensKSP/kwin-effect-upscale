// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The version directive and the OpenGL ES precision declarations are added by
// the loader in scaler.cpp, because a single directive cannot satisfy both
// desktop OpenGL and OpenGL ES. Do not add one here. So is UPSCALE_KERNEL,
// which chooses the filter: 0 bilinear, 1 bicubic, 2 Lanczos.

uniform sampler2D sampler;
uniform vec2 inputSize;
uniform bool intermediate;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"

// A tap, clamped to the picture's own edge so that nothing beside it is read,
// in the same domain EASU filters in; see upscale.frag.
vec3 sampleInput(ivec2 pixel)
{
    vec3 texel = texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(inputSize) - 1), 0).rgb;
#ifdef UPSCALE_DIRECT
    return texel;
#else
    return toWorking(fromDestination(texel));
#endif
}

// Each kernel is separable: a weight for a tap at distance x from the sample
// position, over taps both sides of it. The capture is sampled nearest, so
// even the bilinear one is computed here rather than by the texture unit.
#if UPSCALE_KERNEL == 1
// Catmull-Rom, the cubic with B = 0 and C = 1/2: it passes through every
// pixel and keeps the most detail of Mitchell and Netravali's family.
const int taps = 4;
float weight(float x)
{
    x = abs(x);
    if (x < 1.0) {
        return (1.5 * x - 2.5) * x * x + 1.0;
    }
    return x < 2.0 ? ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0 : 0.0;
}
#elif UPSCALE_KERNEL == 2
// Lanczos with three lobes, sinc(x) sinc(x / 3).
const int taps = 6;
float weight(float x)
{
    if (abs(x) < 1e-5) {
        return 1.0;
    }
    if (abs(x) >= 3.0) {
        return 0.0;
    }
    float angle = 3.14159265358979 * x;
    return 3.0 * sin(angle) * sin(angle / 3.0) / (angle * angle);
}
#else
const int taps = 2;
float weight(float x)
{
    return max(1.0 - abs(x), 0.0);
}
#endif

void main()
{
    // The output pixel's centre in input pixels, where the input pixel
    // centres lie on whole numbers.
    vec2 position = texcoord0 * inputSize - 0.5;
    vec2 base = floor(position);
    vec2 fraction = position - base;
    const int first = 1 - taps / 2;
    float horizontal[taps];
    float vertical[taps];
    vec2 total = vec2(0.0);
    for (int i = 0; i < taps; ++i) {
        horizontal[i] = weight(float(first + i) - fraction.x);
        vertical[i] = weight(float(first + i) - fraction.y);
        total += vec2(horizontal[i], vertical[i]);
    }
    vec3 color = vec3(0.0);
    vec3 darkest = vec3(1e20);
    vec3 brightest = vec3(-1e20);
    for (int j = 0; j < taps; ++j) {
        for (int i = 0; i < taps; ++i) {
            vec3 texel = sampleInput(ivec2(base) + ivec2(first + i, first + j));
            color += texel * (horizontal[i] * vertical[j]);
            darkest = min(darkest, texel);
            brightest = max(brightest, texel);
        }
    }
    // Normalised, because the weights of a windowed kernel do not sum to one.
    // The negative lobes of bicubic and Lanczos overshoot an edge; the result
    // stays within the darkest and brightest pixel the taps read. Past those a
    // halo darker than black becomes negative light in a linear destination,
    // and one past the working encoding's top cannot be decoded at all.
    color = clamp(color / (total.x * total.y), darkest, brightest);
#ifdef UPSCALE_DIRECT
    fragColor = vec4(color, 1.0);
#else
    fragColor = vec4(intermediate ? color : toDestination(fromWorking(color)), 1.0);
#endif
}
