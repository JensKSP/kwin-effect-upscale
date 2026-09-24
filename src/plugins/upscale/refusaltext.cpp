/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Why a window was not scaled, in the words shown to whoever asks. Eligibility
// decides; this says what the decision was. The two are apart because the
// deciding is read while following the rules, and the wording is read while
// translating it or while looking for the sentence a screen showed.

#include "eligibility.h"

#include <KLocalizedString>

namespace KWin
{

static QString describeEffectRefusal(UpscaleRefusal refusal)
{
    switch (refusal) {
    case UpscaleRefusal::Disabled:
        // Only a previous release's own off switch reaches here now: a profile
        // that is switched off takes no part in matching, so its window is
        // unlisted instead, and says so below.
        return i18n("upscaling was switched off.");
    case UpscaleRefusal::Unlisted:
        // The one line that tells being left alone apart from being broken. It
        // names both halves of the rule, so that the person reading it knows
        // there are two ways to change the answer.
        return i18n("the application is not in the list, and unlisted applications are switched off.");
    case UpscaleRefusal::BelowMinimumPixels:
        return i18n("the output pixel count is at or below the configured minimum.");
    case UpscaleRefusal::ResourceFailure:
        return i18n("graphics resource failure; apply settings to retry.");
    case UpscaleRefusal::ScreenLocked:
        return i18n("the screen is locked.");
    case UpscaleRefusal::OtherFullScreenEffect:
        return i18n("another full-screen effect is active.");
    default:
        return QString();
    }
}

// The buffer the client supplied: its size against the destination, and
// whether the scaler can read it as it stands.
static QString describeBufferRefusal(UpscaleRefusal refusal)
{
    switch (refusal) {
    case UpscaleRefusal::NoBuffer:
        return i18n("the window has not supplied a buffer yet.");
    case UpscaleRefusal::EmptyBuffer:
        return i18n("the supplied buffer is empty.");
    case UpscaleRefusal::BufferNotSmaller:
        return i18n("the supplied buffer is not smaller than the destination.");
    case UpscaleRefusal::BufferBelowHalf:
        return i18n("the supplied buffer is less than half the destination size.");
    case UpscaleRefusal::BufferAspectRatio:
        return i18n("the supplied buffer has a different aspect ratio than the destination.");
    case UpscaleRefusal::TransformedBuffer:
        return i18n("the supplied buffer is rotated or flipped.");
    case UpscaleRefusal::CroppedBuffer:
        return i18n("only part of the supplied buffer is displayed.");
    case UpscaleRefusal::TranslucentContent:
        return i18n("the supplied buffer is not fully opaque.");
    case UpscaleRefusal::UnsupportedBufferFormat:
        return i18n("the supplied buffer format cannot be read by the scaler.");
    default:
        return QString();
    }
}

// One paint pass rather than the window. These describe the frame in hand and
// may differ for the next one.
static QString describePassRefusal(UpscaleRefusal refusal)
{
    switch (refusal) {
    case UpscaleRefusal::TransformedPass:
        return i18n("this frame paints the window or the screen with a transformation.");
    case UpscaleRefusal::TranslucentPass:
        return i18n("this frame paints the window with reduced opacity.");
    case UpscaleRefusal::AdjustedPass:
        return i18n("this frame paints the window with adjusted brightness or saturation.");
    case UpscaleRefusal::ScaledPass:
        return i18n("this frame paints at a different scale than the output.");
    case UpscaleRefusal::TransformedRenderTarget:
        return i18n("this frame's render target has an orientation the scaler does not handle.");
    default:
        return QString();
    }
}

QString describeRefusal(UpscaleRefusal refusal)
{
    switch (refusal) {
    case UpscaleRefusal::None:
        return QString();
    case UpscaleRefusal::Disabled:
    case UpscaleRefusal::Unlisted:
    case UpscaleRefusal::BelowMinimumPixels:
    case UpscaleRefusal::ResourceFailure:
    case UpscaleRefusal::ScreenLocked:
    case UpscaleRefusal::OtherFullScreenEffect:
        return describeEffectRefusal(refusal);
    case UpscaleRefusal::SeveralCandidates:
        return i18n("more than one fullscreen window is eligible on this output.");
    case UpscaleRefusal::UnsupportedColors:
        return i18n("this output's colour handling is not supported.");
    case UpscaleRefusal::NoWindow:
        return i18n("there is no window to scale.");
    case UpscaleRefusal::NotFullScreen:
        return i18n("the window is not fullscreen or a selected borderless window covering its output.");
    case UpscaleRefusal::Closing:
        return i18n("the window is closing.");
    case UpscaleRefusal::Minimized:
        return i18n("the window is minimized.");
    case UpscaleRefusal::OtherDesktop:
        return i18n("the window is on another virtual desktop.");
    case UpscaleRefusal::OtherActivity:
        return i18n("the window is on another activity.");
    case UpscaleRefusal::NotActive:
        return i18n("another window on its output is active.");
    case UpscaleRefusal::TranslucentWindow:
        return i18n("the window is translucent.");
    case UpscaleRefusal::NoOutput:
        return i18n("the window is not on an output.");
    case UpscaleRefusal::NoSurface:
        return i18n("the window has no surface to capture.");
    case UpscaleRefusal::TransformedOutput:
        return i18n("the output is rotated or flipped.");
    case UpscaleRefusal::NotCoveringOutput:
        return i18n("the window does not exactly cover its output.");
    case UpscaleRefusal::TransformedWindow:
        return i18n("the window is being transformed.");
    case UpscaleRefusal::ChildSurfaces:
        return i18n("the window's surface has child surfaces.");
    case UpscaleRefusal::TransformedSurface:
        return i18n("the window's surface is being transformed.");
    case UpscaleRefusal::OffsetSurface:
        return i18n("the window's surface is offset inside the window.");
    case UpscaleRefusal::TranslucentSurface:
        return i18n("the window's surface is translucent.");
    case UpscaleRefusal::ResizedSurface:
        return i18n("the window's surface is displayed at a different size than the window.");
    case UpscaleRefusal::NoBuffer:
    case UpscaleRefusal::EmptyBuffer:
    case UpscaleRefusal::BufferNotSmaller:
    case UpscaleRefusal::BufferBelowHalf:
    case UpscaleRefusal::BufferAspectRatio:
    case UpscaleRefusal::TransformedBuffer:
    case UpscaleRefusal::CroppedBuffer:
    case UpscaleRefusal::TranslucentContent:
    case UpscaleRefusal::UnsupportedBufferFormat:
        return describeBufferRefusal(refusal);
    case UpscaleRefusal::TransformedPass:
    case UpscaleRefusal::TranslucentPass:
    case UpscaleRefusal::AdjustedPass:
    case UpscaleRefusal::ScaledPass:
    case UpscaleRefusal::TransformedRenderTarget:
        return describePassRefusal(refusal);
    }
    return QString();
}

} // namespace KWin
