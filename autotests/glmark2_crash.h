/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Copied into a private copy of glmark2 by tools/prepare-crash-game.py, so
// that a real game crashes at a moment a test chooses: a game can disappear
// without warning, and whatever the effect arranged for it has to survive
// that. UPSCALE_TEST_CRASH names the moment:
//
//   bind        the outputs are bound and their modes have arrived, before
//               any window exists
//   window      the compositor's first configure has arrived, before the
//               first frame is committed
//   resize      an X11 window is told a size other than the one it was
//               created with
//   frame:N     the Nth frame has been presented
//
// Each crash first writes one line to standard error naming the moment, and
// the size the game was told or the frame it reached, so a test can tell what
// the effect had done before the game went away. Unset, nothing changes.
//
// The copy also names its process on its X11 window (_NET_WM_PID), as SDL and
// most toolkits do and glmark2 does not: it is how KWin 6.3 knows which
// program an X11 window belongs to, and so how a profile finds the game.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

inline const char *upscaleTestCrashPoint()
{
    const char *point = std::getenv("UPSCALE_TEST_CRASH");
    return point ? point : "";
}

inline bool upscaleTestCrashesAt(const char *point)
{
    return std::strcmp(upscaleTestCrashPoint(), point) == 0;
}

[[noreturn]] inline void upscaleTestCrash(const char *point, int width, int height)
{
    std::fprintf(stderr, "UPSCALE_TEST_CRASH %s %dx%d\n", point, width, height);
    std::fflush(stderr);
    std::abort();
}

inline void upscaleTestCrashAfterFrame()
{
    static long frames = 0;
    const char *point = upscaleTestCrashPoint();
    if (std::strncmp(point, "frame:", 6) == 0 && ++frames >= std::atol(point + 6)) {
        std::fprintf(stderr, "UPSCALE_TEST_CRASH frame %ld\n", frames);
        std::fflush(stderr);
        std::abort();
    }
}
