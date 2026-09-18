/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace KWin
{

/**
 * Whether this binary was compiled as a Debug build.
 *
 * CMake defines NDEBUG for Release, RelWithDebInfo and MinSizeRel and leaves
 * it undefined for Debug, which is exactly the distinction the on-screen
 * display defaults need: debug symbols alone do not make a Debug build, and a
 * multi-configuration build carries the configuration of the built binary.
 *
 * It decides defaults only. An explicit preference is read from the
 * configuration and survives a change of build type.
 */
inline constexpr bool upscaleDebugBuild =
#ifdef NDEBUG
    false
#else
    true
#endif
    ;

} // namespace KWin
