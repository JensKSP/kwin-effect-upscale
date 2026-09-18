// SPDX-FileCopyrightText: 2021 Advanced Micro Devices, Inc.
// SPDX-License-Identifier: MIT
//
//------------------------------------------------------------------------------------------------------------------------------
// FidelityFX Super Resolution Sample
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// FP32 RCAS from AMD FSR 1 v1.20210629, with vector arithmetic and
// explicit guards for constant black/white. Strength multiplies the lobe;
// zero is handled by bypassing this pass in the renderer.
vec3 rcas(ivec2 pixel)
{
    vec3 b = loadPixel(pixel + ivec2(0, -1));
    vec3 d = loadPixel(pixel + ivec2(-1, 0));
    vec3 e = loadPixel(pixel);
    vec3 f = loadPixel(pixel + ivec2(1, 0));
    vec3 h = loadPixel(pixel + ivec2(0, 1));
    vec3 minimum = min(min(b, d), min(f, h));
    vec3 maximum = max(max(b, d), max(f, h));
    vec3 hitMinimum = min(minimum, e) / max(4.0 * maximum, vec3(1e-20));
    vec3 hitMaximum = (1.0 - max(maximum, e)) / min(4.0 * minimum - 4.0, vec3(-1e-20));
    vec3 lobes = max(-hitMinimum, hitMaximum);
    float lobe = max(-0.1875, min(max(lobes.r, max(lobes.g, lobes.b)), 0.0)) * strength;
    return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}
