// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later

uniform int destinationTransferFunction;
uniform vec2 destinationLuminance;
uniform float referenceLuminance;

// The capture has already passed through KWin's gamut mapping and tone mapping
// into the destination encoding. Decode just its transfer function into
// linear units of reference white, preserving signed linear values.
// Linear light is put into a bounded, approximately gamma-2 working space.
// A signed mapping retains negative components without an SDR clamp. RGBA32F
// is required: half precision near 1 would lose HDR highlight distinctions.
// This reversible encoding is a filter-domain choice, not display tone mapping.
vec3 fromDestination(vec3 color)
{
    if (destinationTransferFunction == 0) {
        color = mix(pow(max((color + 0.055) / 1.055, vec3(0.0)), vec3(2.4)),
                    color / 12.92, lessThanEqual(color, vec3(0.04045)));
    } else if (destinationTransferFunction == 2) {
        vec3 powered = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 78.84375));
        color = pow(max(powered - 0.8359375, vec3(0.0)) / (18.8515625 - 18.6875 * powered), vec3(1.0 / 0.1593017578125));
    } else if (destinationTransferFunction == 3) {
        color = pow(max(color, vec3(0.0)), vec3(2.2));
    }
    return (color * destinationLuminance.y + destinationLuminance.x) / referenceLuminance;
}

vec3 toWorking(vec3 color)
{
    return 0.5 + 0.5 * sign(color) * sqrt(abs(color) / (1.0 + abs(color)));
}

vec3 fromWorking(vec3 color)
{
    vec3 signedRoot = 2.0 * color - 1.0;
    vec3 magnitude = signedRoot * signedRoot;
    return sign(signedRoot) * magnitude / max(1.0 - magnitude, vec3(1e-7));
}

// Only the destination transfer function remains. Do not run a second gamut
// transform or tone mapper after filtering. Enum values follow KWin's
// TransferFunction, and the luminance parameters come from the render target.
vec3 toDestination(vec3 color)
{
    color = (color * referenceLuminance - destinationLuminance.x) / destinationLuminance.y;
    if (destinationTransferFunction == 0) {
        color = clamp(color, 0.0, 1.0);
        return mix(1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
                   12.92 * color, lessThanEqual(color, vec3(0.0031308)));
    }
    if (destinationTransferFunction == 2) {
        vec3 powered = pow(clamp(color, 0.0, 1.0), vec3(0.1593017578125));
        return pow((0.8359375 + 18.8515625 * powered) / (1.0 + 18.6875 * powered), vec3(78.84375));
    }
    if (destinationTransferFunction == 3) {
        return pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    return color;
}
