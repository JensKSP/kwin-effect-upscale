// SPDX-FileCopyrightText: 2025 Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSD-3-Clause
//
// Snapdragon Game Super Resolution 1, from Qualcomm's
// sgsr/v1/include/glsl/sgsr1_shader_mobile.frag at commit d926f074, in its
// RGBA operation mode, which finds edges in the green channel.
//
// Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Changed from Qualcomm's shader: the version directive and precision come
// from the loader in scaler.cpp, which also chooses the filter domain; the
// twelve taps that textureGather collected are fetched one by one, because the
// capture is sampled nearest and OpenGL ES 3.0 has no gather, and so is the
// bilinear base tap; edgeSharpness is a uniform the effect's strength sets;
// and in a linear destination the filter runs on the working encoding shifted
// into -1 to 1, with its output kept in that range rather than in 0 to 1.

uniform sampler2D sampler;
uniform vec2 inputSize;
uniform float edgeSharpness;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"

// Qualcomm's thresholds are differences on a signal that fills zero to one,
// which an encoded destination is. The working encoding spends its lower half
// on negative linear values, so ordinary content spans about a third of it;
// in 2 * value - 1, as RCAS runs there, its differences are twice as large and
// the bound on the result is the encoding's own.
#ifdef UPSCALE_DIRECT
const float signalFloor = 0.0;
#else
const float signalFloor = -1.0;
#endif

vec3 sampleInput(ivec2 pixel)
{
    vec3 texel = texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(inputSize) - 1), 0).rgb;
#ifdef UPSCALE_DIRECT
    return texel;
#else
    return 2.0 * toWorking(fromDestination(texel)) - 1.0;
#endif
}

float green(ivec2 pixel)
{
    return sampleInput(pixel).g;
}

float fastLanczos2(float x)
{
    float wA = x - 4.0;
    float wB = x * wA - wA;
    wA *= wA;
    return wB * wA;
}

vec2 weightY(float dx, float dy, float c, float std)
{
    float x = ((dx * dx) + (dy * dy)) * 0.55 + clamp(abs(c) * std, 0.0, 1.0);
    float w = fastLanczos2(x);
    return vec2(w, w * c);
}

void main()
{
    const float edgeThreshold = 8.0 / 255.0;

    // Where Qualcomm gathers around imgCoordPixel, the four texels nearest the
    // sample are (f.x, f.y) to (f.x + 1, f.y + 1), and pl is the position
    // between them.
    vec2 position = texcoord0 * inputSize - 0.5;
    ivec2 f = ivec2(floor(position));
    vec2 pl = position - floor(position);

    vec3 c00 = sampleInput(f);
    vec3 c10 = sampleInput(f + ivec2(1, 0));
    vec3 c01 = sampleInput(f + ivec2(0, 1));
    vec3 c11 = sampleInput(f + ivec2(1, 1));
    vec4 color;
    color.xyz = mix(mix(c00, c10, pl.x), mix(c01, c11, pl.x), pl.y);

    // The components Qualcomm's gathers return: left is (x - 1, y + 1),
    // (x, y + 1), (x, y), (x - 1, y) around f, right the same two columns to
    // the right of the centre, upDown the two texels below and above it.
    vec4 left = vec4(green(f + ivec2(-1, 1)), c01.g, c00.g, green(f + ivec2(-1, 0)));

    float edgeVote = abs(left.z - left.y) + abs(color.y - left.y) + abs(color.y - left.z);
    if (edgeVote > edgeThreshold) {
        vec4 right = vec4(c11.g, green(f + ivec2(2, 1)), green(f + ivec2(2, 0)), c10.g);
        vec4 upDown = vec4(green(f + ivec2(0, -1)), green(f + ivec2(1, -1)), green(f + ivec2(1, 2)), green(f + ivec2(0, 2)));

        float mean = (left.y + left.z + right.x + right.w) * 0.25;
        left = left - vec4(mean);
        right = right - vec4(mean);
        upDown = upDown - vec4(mean);
        color.w = color.y - mean;

        float sum = (((((abs(left.x) + abs(left.y)) + abs(left.z)) + abs(left.w)) + (((abs(right.x) + abs(right.y)) + abs(right.z)) + abs(right.w))) + (((abs(upDown.x) + abs(upDown.y)) + abs(upDown.z)) + abs(upDown.w)));
        float std = 2.181818 / sum;

        vec2 aWY = weightY(pl.x, pl.y + 1.0, upDown.x, std);
        aWY += weightY(pl.x - 1.0, pl.y + 1.0, upDown.y, std);
        aWY += weightY(pl.x - 1.0, pl.y - 2.0, upDown.z, std);
        aWY += weightY(pl.x, pl.y - 2.0, upDown.w, std);
        aWY += weightY(pl.x + 1.0, pl.y - 1.0, left.x, std);
        aWY += weightY(pl.x, pl.y - 1.0, left.y, std);
        aWY += weightY(pl.x, pl.y, left.z, std);
        aWY += weightY(pl.x + 1.0, pl.y, left.w, std);
        aWY += weightY(pl.x - 1.0, pl.y - 1.0, right.x, std);
        aWY += weightY(pl.x - 2.0, pl.y - 1.0, right.y, std);
        aWY += weightY(pl.x - 2.0, pl.y, right.z, std);
        aWY += weightY(pl.x - 1.0, pl.y, right.w, std);

        float finalY = aWY.y / aWY.x;

        float maxY = max(max(left.y, left.z), max(right.x, right.w));
        float minY = min(min(left.y, left.z), min(right.x, right.w));
        finalY = clamp(edgeSharpness * finalY, minY, maxY);

        float deltaY = finalY - color.w;

        // smooth high contrast input
        deltaY = clamp(deltaY, -23.0 / 255.0, 23.0 / 255.0);

        color.xyz = clamp(color.xyz + deltaY, signalFloor, 1.0);
    }

#ifdef UPSCALE_DIRECT
    fragColor = vec4(color.xyz, 1.0);
#else
    fragColor = vec4(toDestination(fromWorking(0.5 * (color.xyz + 1.0))), 1.0);
#endif
}
