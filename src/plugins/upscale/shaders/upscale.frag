#version 140
// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef GL_ES
// Float arithmetic alone does not raise the sampler's default precision.
// Low-precision texture results would discard HDR detail before filtering.
precision highp sampler2D;
precision highp int;
#endif

uniform sampler2D sampler;
uniform vec2 inputSize;
uniform vec2 outputSize;
uniform bool intermediate;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"

vec3 sampleInput(ivec2 pixel)
{
    return toWorking(fromDestination(texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(inputSize) - 1), 0).rgb));
}

// Emulate gather with texelFetch for OpenGL ES 3.0. Texture storage coordinates
// are used throughout; the final GLTexture draw applies its content transform.
vec4 gatherChannel(vec2 position, int channel)
{
    ivec2 pixel = ivec2(floor(position * inputSize - 0.5));
    return vec4(sampleInput(pixel + ivec2(0, 1))[channel],
                sampleInput(pixel + ivec2(1, 1))[channel],
                sampleInput(pixel + ivec2(1, 0))[channel],
                sampleInput(pixel)[channel]);
}
vec4 FsrEasuRF(vec2 position) { return gatherChannel(position, 0); }
vec4 FsrEasuGF(vec2 position) { return gatherChannel(position, 1); }
vec4 FsrEasuBF(vec2 position) { return gatherChannel(position, 2); }

#include "upscale/easu.glsl"

void main()
{
    vec2 ratio = inputSize / outputSize;
    vec3 color;
    FsrEasuF(color, floor(texcoord0 * outputSize),
             vec4(ratio, 0.5 * ratio - 0.5),
             vec4(1.0, 1.0, 1.0, -1.0) / inputSize.xyxy,
             vec4(-1.0, 2.0, 1.0, 2.0) / inputSize.xyxy,
             vec4(0.0, 4.0, 0.0, 0.0) / inputSize.xyxy);
    fragColor = vec4(intermediate ? color : toDestination(fromWorking(color)), 1.0);
}
