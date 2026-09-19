/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <QString>

namespace KWin
{

class EffectWindow;
class SurfaceItem;

/**
 * The one condition that kept a window from being scaled.
 *
 * Eligibility is a single yes or no to the effect, but a person looking at a
 * game that is not being scaled needs to know which of the two dozen
 * conditions said no; the general rule tells them nothing they can act on.
 * Every condition therefore has its own value here, and the checks below
 * report the first one that failed.
 *
 * The values are grouped by what would have to change to clear them: the
 * effect's own state, the window, the surface inside it, the buffer the client
 * supplied, and finally the individual paint pass, which describes one frame
 * rather than the window and may differ for the next frame.
 */
enum class UpscaleRefusal {
    None,

    Disabled,
    NativeRule,
    BelowMinimumPixels,
    ResourceFailure,
    ScreenLocked,
    OtherFullScreenEffect,
    SeveralCandidates,
    UnsupportedColors,

    NoWindow,
    NotFullScreen,
    Closing,
    Minimized,
    OtherDesktop,
    OtherActivity,
    TranslucentWindow,
    NoOutput,
    NoSurface,
    TransformedOutput,
    NotCoveringOutput,
    TransformedWindow,

    ChildSurfaces,
    TransformedSurface,
    OffsetSurface,
    TranslucentSurface,
    ResizedSurface,

    NoBuffer,
    EmptyBuffer,
    BufferNotSmaller,
    BufferBelowHalf,
    BufferAspectRatio,
    TransformedBuffer,
    CroppedBuffer,
    TranslucentContent,
    UnsupportedBufferFormat,

    TransformedPass,
    TranslucentPass,
    AdjustedPass,
    ScaledPass,
    TransformedRenderTarget,
};

/** Fullscreen, or a profiled borderless window covering its own output. */
bool upscalePresentation(EffectWindow *window);

/**
 * Why this window cannot be scaled, or None when only the effect's own state
 * or another window can still refuse it.
 *
 * This runs for every window of every frame, so it answers with the reason
 * alone and allocates nothing; the description is built only when something
 * asks for one.
 */
UpscaleRefusal windowRefusal(EffectWindow *window);

/** Whether this paint pass may be replaced by a scaled one. */
UpscaleRefusal passRefusal(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                           int mask, const WindowPaintData &data);

/** One sentence naming the condition, for settings, logs and the overlay. */
QString describeRefusal(UpscaleRefusal refusal);

/**
 * The format of the buffer currently behind this surface, as its DRM
 * four-character code. Reported alongside a refused format, because the code
 * says which client behaviour to look at and the refusal alone does not.
 */
QString describeSuppliedFormat(SurfaceItem *surface);

} // namespace KWin
