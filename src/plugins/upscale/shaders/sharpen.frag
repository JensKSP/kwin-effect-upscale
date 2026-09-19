// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The version directive and the OpenGL ES precision declarations are added by
// the loader in scaler.cpp, because a single directive cannot satisfy both
// desktop OpenGL and OpenGL ES. Do not add one here.

uniform sampler2D sampler;
uniform vec2 outputSize;
uniform float strength;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"

// RCAS derives its noise limiter from absolute levels and assumes a signal
// that fills 0 to 1. The working encoding spends its lower half on negative
// linear values, so ordinary content would only ever reach 0.5 to 1 and the
// limiter would see half the contrast it was written for. Shifting the pass
// into the 2 * value - 1 domain restores that range. The filter itself,
// (lobe * (b + d + f + h) + e) / (4 * lobe + 1), is affine-equivariant: the
// shift cancels exactly, so only the limiter's decision changes. Negative
// linear values stay below zero here, where the guarded divisions yield a
// zero lobe and leave the filtered pixel unsharpened rather than clamped.
vec3 loadPixel(ivec2 pixel)
{
    vec3 texel = texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(outputSize) - 1), 0).rgb;
#ifdef UPSCALE_DIRECT
    // Already the zero to one signal RCAS was written for.
    return texel;
#else
    return 2.0 * texel - 1.0;
#endif
}

#include "upscale/rcas.glsl"

void main()
{
#ifdef UPSCALE_DIRECT
    fragColor = vec4(rcas(ivec2(texcoord0 * outputSize)), 1.0);
#else
    vec3 sharpened = 0.5 * (rcas(ivec2(texcoord0 * outputSize)) + 1.0);
    fragColor = vec4(toDestination(fromWorking(sharpened)), 1.0);
#endif
}
