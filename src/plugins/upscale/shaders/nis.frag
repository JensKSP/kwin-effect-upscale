// SPDX-FileCopyrightText: 2022 NVIDIA CORPORATION & AFFILIATES
// SPDX-License-Identifier: MIT
//
// The MIT License(MIT)
//
// Copyright(c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files(the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// NVScaler from the NVIDIA Image Scaling SDK v1.0.3, NIS/NIS_Scaler.h at
// commit 35e13ba3, as a fragment program.
//
// Changed from NVIDIA's shader: NVIDIA's compute shader loads a tile of luma
// and edge maps into shared memory for a block of output pixels; here each
// output pixel reads its own six by six luma neighbourhood and derives the
// four edge maps it interpolates from it, which is the same arithmetic on the
// same texels. The filter banks are constants (nis_coefficients.glsl), the
// configuration NVScalerUpdateConfig computes arrives as uniforms (nisconfig.h),
// and the bilinear tap for the colour is computed from four texel fetches,
// because the capture is sampled nearest. Arrays are one-dimensional, which
// is all OpenGL ES 3.0 has. The version directive and precision come from the
// loader in scaler.cpp. An encoded destination is NIS_HDR_MODE_NONE or, with
// the PQ luma weights, NIS_HDR_MODE_PQ, and its output is clamped to zero to
// one as NIS_CLAMP_OUTPUT does; a linear destination is NIS_HDR_MODE_LINEAR on
// values decoded to NVIDIA's unit of 80 nits, unclamped, with its luma kept
// from going negative before the square root.

uniform sampler2D sampler;
uniform vec2 inputSize;
// kDetectRatio, kDetectThres, kMinContrastRatio, kRatioNorm.
uniform vec4 detection;
// kSharpStartY, kSharpScaleY, kSharpStrengthMin, kSharpStrengthScale.
uniform vec4 sharpness;
// kSharpLimitMin, kSharpLimitScale.
uniform vec2 sharpnessLimit;
// getY's weights: BT.709's, or NVIDIA's for PQ.
uniform vec3 lumaWeights;
in vec2 texcoord0;
out vec4 fragColor;

#include "upscale/workingcolor.glsl"
#include "upscale/nis_coefficients.glsl"

const int kPhaseCount = 64;
const float kEps = 1.0 / 255.0;
const float kContrastBoost = 1.0;
const float kHDRCompressionFactor = 0.282842712;

// The neighbourhood, row by row: luma[i * 6 + j] is the texel j - 2 columns
// and i - 2 rows from the one at or before the sample position.
float luma[36];
#define P(i, j) luma[(i) * 6 + (j)]

vec3 sampleInput(ivec2 pixel)
{
    vec3 texel = texelFetch(sampler, clamp(pixel, ivec2(0), ivec2(inputSize) - 1), 0).rgb;
#ifdef UPSCALE_DIRECT
    return texel;
#else
    return fromDestination(texel) * referenceLuminance / 80.0;
#endif
}

float getY(vec3 rgba)
{
#ifdef UPSCALE_DIRECT
    return dot(lumaWeights, rgba);
#else
    return sqrt(max(dot(vec3(0.2126, 0.7152, 0.0722), rgba), 0.0)) * kHDRCompressionFactor;
#endif
}

float getYLinear(vec3 rgba)
{
    return 0.2126 * rgba.x + 0.7152 * rgba.y + 0.0722 * rgba.z;
}

vec4 GetEdgeMap(int i, int j)
{
    float kDetectRatio = detection.x;
    float kDetectThres = detection.y;
    float g_0 = abs(P(0 + i, 0 + j) + P(0 + i, 1 + j) + P(0 + i, 2 + j) - P(2 + i, 0 + j) - P(2 + i, 1 + j) - P(2 + i, 2 + j));
    float g_45 = abs(P(1 + i, 0 + j) + P(0 + i, 0 + j) + P(0 + i, 1 + j) - P(2 + i, 1 + j) - P(2 + i, 2 + j) - P(1 + i, 2 + j));
    float g_90 = abs(P(0 + i, 0 + j) + P(1 + i, 0 + j) + P(2 + i, 0 + j) - P(0 + i, 2 + j) - P(1 + i, 2 + j) - P(2 + i, 2 + j));
    float g_135 = abs(P(1 + i, 0 + j) + P(2 + i, 0 + j) + P(2 + i, 1 + j) - P(0 + i, 1 + j) - P(0 + i, 2 + j) - P(1 + i, 2 + j));

    float g_0_90_max = max(g_0, g_90);
    float g_0_90_min = min(g_0, g_90);
    float g_45_135_max = max(g_45, g_135);
    float g_45_135_min = min(g_45, g_135);

    float e_0_90 = 0.0;
    float e_45_135 = 0.0;

    if (g_0_90_max + g_45_135_max == 0.0) {
        return vec4(0.0);
    }

    e_0_90 = min(g_0_90_max / (g_0_90_max + g_45_135_max), 1.0);
    e_45_135 = 1.0 - e_0_90;

    bool c_0_90 = (g_0_90_max > (g_0_90_min * kDetectRatio)) && (g_0_90_max > kDetectThres) && (g_0_90_max > g_45_135_min);
    bool c_45_135 = (g_45_135_max > (g_45_135_min * kDetectRatio)) && (g_45_135_max > kDetectThres) && (g_45_135_max > g_0_90_min);
    bool c_g_0_90 = g_0_90_max == g_0;
    bool c_g_45_135 = g_45_135_max == g_45;

    float f_e_0_90 = (c_0_90 && c_45_135) ? e_0_90 : 1.0;
    float f_e_45_135 = (c_0_90 && c_45_135) ? e_45_135 : 1.0;

    float weight_0 = (c_0_90 && c_g_0_90) ? f_e_0_90 : 0.0;
    float weight_90 = (c_0_90 && !c_g_0_90) ? f_e_0_90 : 0.0;
    float weight_45 = (c_45_135 && c_g_45_135) ? f_e_45_135 : 0.0;
    float weight_135 = (c_45_135 && !c_g_45_135) ? f_e_45_135 : 0.0;

    return vec4(weight_0, weight_90, weight_45, weight_135);
}

float CalcLTI(float p0, float p1, float p2, float p3, float p4, float p5, int phase_index)
{
    float kMinContrastRatio = detection.z;
    float kRatioNorm = detection.w;
    bool selector = (phase_index <= kPhaseCount / 2);
    float sel = selector ? p0 : p3;
    float a_min = min(min(p1, p2), sel);
    float a_max = max(max(p1, p2), sel);
    sel = selector ? p2 : p5;
    float b_min = min(min(p3, p4), sel);
    float b_max = max(max(p3, p4), sel);

    float a_cont = a_max - a_min;
    float b_cont = b_max - b_min;

    float cont_ratio = max(a_cont, b_cont) / (min(a_cont, b_cont) + kEps);
    return (1.0 - clamp((cont_ratio - kMinContrastRatio) * kRatioNorm, 0.0, 1.0)) * kContrastBoost;
}

float EvalPoly6(float pxl[6], int phase_int)
{
    float kSharpStartY = sharpness.x;
    float kSharpScaleY = sharpness.y;
    float kSharpStrengthMin = sharpness.z;
    float kSharpStrengthScale = sharpness.w;
    float kSharpLimitMin = sharpnessLimit.x;
    float kSharpLimitScale = sharpnessLimit.y;
    float y = 0.0;
    for (int i = 0; i < 6; ++i) {
        y += coefScale[phase_int * 6 + i] * pxl[i];
    }
    float y_usm = 0.0;
    for (int i = 0; i < 6; ++i) {
        y_usm += coefUsm[phase_int * 6 + i] * pxl[i];
    }

    // let's compute a piece-wise ramp based on luma
    float y_scale = 1.0 - clamp((y - kSharpStartY) * kSharpScaleY, 0.0, 1.0);

    // scale the ramp to sharpen as a function of luma
    float y_sharpness = y_scale * kSharpStrengthScale + kSharpStrengthMin;

    y_usm *= y_sharpness;

    // scale the ramp to limit USM as a function of luma
    float y_sharpness_limit = (y_scale * kSharpLimitScale + kSharpLimitMin) * y;

    y_usm = min(y_sharpness_limit, max(-y_sharpness_limit, y_usm));
    // reduce ringing
    y_usm *= CalcLTI(pxl[0], pxl[1], pxl[2], pxl[3], pxl[4], pxl[5], phase_int);

    return y + y_usm;
}

float FilterNormal(int phase_x_frac_int, int phase_y_frac_int)
{
    float h_acc = 0.0;
    for (int j = 0; j < 6; ++j) {
        float v_acc = 0.0;
        for (int i = 0; i < 6; ++i) {
            v_acc += P(i, j) * coefScale[phase_y_frac_int * 6 + i];
        }
        h_acc += v_acc * coefScale[phase_x_frac_int * 6 + j];
    }

    // let's return the sum unpacked -> we can accumulate it later
    return h_acc;
}

float AddDirFilters(float phase_x_frac, float phase_y_frac, int phase_x_frac_int, int phase_y_frac_int, vec4 w)
{
    float f = 0.0;
    if (w.x > 0.0) {
        // 0 deg filter
        float interp0Deg[6];
        for (int i = 0; i < 6; ++i) {
            interp0Deg[i] = mix(P(i, 2), P(i, 3), phase_x_frac);
        }
        f += EvalPoly6(interp0Deg, phase_y_frac_int) * w.x;
    }
    if (w.y > 0.0) {
        // 90 deg filter
        float interp90Deg[6];
        for (int i = 0; i < 6; ++i) {
            interp90Deg[i] = mix(P(2, i), P(3, i), phase_y_frac);
        }
        f += EvalPoly6(interp90Deg, phase_x_frac_int) * w.y;
    }
    if (w.z > 0.0) {
        // 45 deg filter
        float pphase_b45 = 0.5 + 0.5 * (phase_x_frac - phase_y_frac);

        float temp_interp45Deg[7];
        temp_interp45Deg[1] = mix(P(2, 1), P(1, 2), pphase_b45);
        temp_interp45Deg[3] = mix(P(3, 2), P(2, 3), pphase_b45);
        temp_interp45Deg[5] = mix(P(4, 3), P(3, 4), pphase_b45);
        {
            pphase_b45 = pphase_b45 - 0.5;
            float a = (pphase_b45 >= 0.0) ? P(0, 2) : P(2, 0);
            float b = (pphase_b45 >= 0.0) ? P(1, 3) : P(3, 1);
            float c = (pphase_b45 >= 0.0) ? P(2, 4) : P(4, 2);
            float d = (pphase_b45 >= 0.0) ? P(3, 5) : P(5, 3);
            temp_interp45Deg[0] = mix(P(1, 1), a, abs(pphase_b45));
            temp_interp45Deg[2] = mix(P(2, 2), b, abs(pphase_b45));
            temp_interp45Deg[4] = mix(P(3, 3), c, abs(pphase_b45));
            temp_interp45Deg[6] = mix(P(4, 4), d, abs(pphase_b45));
        }

        float interp45Deg[6];
        float pphase_p45 = phase_x_frac + phase_y_frac;
        if (pphase_p45 >= 1.0) {
            for (int i = 0; i < 6; i++) {
                interp45Deg[i] = temp_interp45Deg[i + 1];
            }
            pphase_p45 = pphase_p45 - 1.0;
        } else {
            for (int i = 0; i < 6; i++) {
                interp45Deg[i] = temp_interp45Deg[i];
            }
        }

        f += EvalPoly6(interp45Deg, min(int(pphase_p45 * 64.0), kPhaseCount - 1)) * w.z;
    }
    if (w.w > 0.0) {
        // 135 deg filter
        float pphase_b135 = 0.5 * (phase_x_frac + phase_y_frac);

        float temp_interp135Deg[7];
        temp_interp135Deg[1] = mix(P(3, 1), P(4, 2), pphase_b135);
        temp_interp135Deg[3] = mix(P(2, 2), P(3, 3), pphase_b135);
        temp_interp135Deg[5] = mix(P(1, 3), P(2, 4), pphase_b135);
        {
            pphase_b135 = pphase_b135 - 0.5;
            float a = (pphase_b135 >= 0.0) ? P(5, 2) : P(3, 0);
            float b = (pphase_b135 >= 0.0) ? P(4, 3) : P(2, 1);
            float c = (pphase_b135 >= 0.0) ? P(3, 4) : P(1, 2);
            float d = (pphase_b135 >= 0.0) ? P(2, 5) : P(0, 3);
            temp_interp135Deg[0] = mix(P(4, 1), a, abs(pphase_b135));
            temp_interp135Deg[2] = mix(P(3, 2), b, abs(pphase_b135));
            temp_interp135Deg[4] = mix(P(2, 3), c, abs(pphase_b135));
            temp_interp135Deg[6] = mix(P(1, 4), d, abs(pphase_b135));
        }

        float interp135Deg[6];
        float pphase_p135 = 1.0 + (phase_x_frac - phase_y_frac);
        if (pphase_p135 >= 1.0) {
            for (int i = 0; i < 6; ++i) {
                interp135Deg[i] = temp_interp135Deg[i + 1];
            }
            pphase_p135 = pphase_p135 - 1.0;
        } else {
            for (int i = 0; i < 6; ++i) {
                interp135Deg[i] = temp_interp135Deg[i];
            }
        }

        f += EvalPoly6(interp135Deg, min(int(pphase_p135 * 64.0), kPhaseCount - 1)) * w.w;
    }
    return f;
}

void main()
{
    // The source position of this output pixel's centre, its nearest integer
    // part and fractional part, and the discretised phase: srcX, floor(srcX),
    // fx and fx_int in NVScaler.
    vec2 source = texcoord0 * inputSize - 0.5;
    vec2 nearest = floor(source);
    vec2 fraction = source - nearest;
    int fx_int = min(int(fraction.x * float(kPhaseCount)), kPhaseCount - 1);
    int fy_int = min(int(fraction.y * float(kPhaseCount)), kPhaseCount - 1);

    // Load the 6x6 support, keeping the colour of the four texels around the
    // sample for the bilinear tap.
    ivec2 corner = ivec2(nearest) - 2;
    vec3 c00 = vec3(0.0);
    vec3 c10 = vec3(0.0);
    vec3 c01 = vec3(0.0);
    vec3 c11 = vec3(0.0);
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            vec3 texel = sampleInput(corner + ivec2(j, i));
            P(i, j) = getY(texel);
            if (i == 2 && j == 2) {
                c00 = texel;
            } else if (i == 2 && j == 3) {
                c10 = texel;
            } else if (i == 3 && j == 2) {
                c01 = texel;
            } else if (i == 3 && j == 3) {
                c11 = texel;
            }
        }
    }

    // The edge maps of the four texels around the sample, each from the
    // three by three around it, interpolated to the sample position.
    vec4 h0 = mix(GetEdgeMap(1, 1), GetEdgeMap(1, 2), fraction.x);
    vec4 h1 = mix(GetEdgeMap(2, 1), GetEdgeMap(2, 2), fraction.x);
    vec4 w = mix(h0, h1, fraction.y);

    // weight for luma
    float baseWeight = 1.0 - w.x - w.y - w.z - w.w;

    // final luma is a weighted product of directional & normal filters
    float opY = 0.0;

    // get traditional scaler filter output
    opY += FilterNormal(fx_int, fy_int) * baseWeight;

    // get directional filter bank output
    opY += AddDirFilters(fraction.x, fraction.y, fx_int, fy_int, w);

    // do bilinear tap for chroma upscaling
    vec3 op = mix(mix(c00, c10, fraction.x), mix(c01, c11, fraction.x), fraction.y);

#ifdef UPSCALE_DIRECT
    float y = getY(op);
    float corr = opY - y;
    fragColor = vec4(clamp(op + corr, 0.0, 1.0), 1.0);
#else
    const float kLinearEps = 1e-4;
    const float kNorm = 1.0 / kHDRCompressionFactor;
    float opYN = max(opY, 0.0) * kNorm;
    float corr = (opYN * opYN + kLinearEps) / (max(getYLinear(op), 0.0) + kLinearEps);
    fragColor = vec4(toDestination(op * corr * 80.0 / referenceLuminance), 1.0);
#endif
}
