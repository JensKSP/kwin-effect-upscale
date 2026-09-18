/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resolution.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

static void require(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size != 24) {
        return 0;
    }
    // Decode explicitly: unaligned loads and host byte order must not change
    // a saved reproducer. Full signed inputs exercise overflow boundaries.
    std::array<int, 6> values;
    for (size_t index = 0; index < values.size(); ++index) {
        const size_t offset = index * 4;
        const uint32_t bits = uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8)
            | (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
        values[index] = std::bit_cast<int32_t>(bits);
    }
    using namespace KWin;
    const UpscaleSize input{values[0], values[1]};
    const UpscaleSize output{values[2], values[3]};
    const auto preset = static_cast<ResolutionPreset>(uint32_t(values[4]) % 7);
    const UpscaleSize desired = desiredResolution(output, preset, values[5]);
    if (output.width > 0 && output.height > 0) {
        require(desired.width >= output.width / 2 && desired.width <= output.width);
        require(desired.height >= output.height / 2 && desired.height <= output.height);
    }
    require(!canUpscale(input, input));
    require(canUpscale(input, output) == canUpscale({input.height, input.width}, {output.height, output.width}));
    if (canUpscale(input, output)) {
        require(input.width > 0 && input.height > 0);
        require(input.width < output.width && input.height < output.height);
        require(int64_t(input.width) * 2 >= output.width && int64_t(input.height) * 2 >= output.height);
    }
    const double strength = sharpeningAmount(true, values[5]);
    require(strength >= 0 && strength <= 1);
    require(sharpeningAmount(false, values[5]) == 0);
    return 0;
}
