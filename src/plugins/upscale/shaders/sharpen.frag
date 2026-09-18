#version 140
// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef GL_ES
precision highp sampler2D;
precision highp int;
#endif

uniform sampler2D sampler;
uniform vec2 outputSize;
uniform float strength;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"

vec3 loadPixel(ivec2 pixel)
{
    return texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(outputSize) - 1), 0).rgb;
}

#include "upscale/rcas.glsl"

void main()
{
    fragColor = vec4(toDestination(fromWorking(rcas(ivec2(texcoord0 * outputSize)))), 1.0);
}
