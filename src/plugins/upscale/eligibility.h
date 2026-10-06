/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"
#include "picture.h"
#include "presentation.h"

#include <QRectF>
#include <QSize>
#include <QString>

#include <functional>

namespace KWin
{

class EffectWindow;
class Window;
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
/**
 * How the client's buffer reached the compositor.
 *
 * This is as close to "what did it render with" as a compositor can honestly
 * get. Neither protocol carries the client's graphics API, and every client
 * arrives as a buffer either way, so naming OpenGL or Vulkan here would be a
 * guess. Whether the buffer came from the GPU or through main memory is not.
 */
enum class UpscaleBufferKind {
    Unknown,
    Gpu,
    SharedMemory,
};

/**
 * Who enlarges an X11 window this effect made smaller than its output.
 *
 * Xwayland does it for a client that established an emulated mode on its own
 * connection: a viewport carries the buffer to the output and its input
 * coordinates come back scaled. For every other client the effect does it,
 * sizing the surface item to the frame so that KWin paints, damages and
 * clips the window as covering the output, and mapping pointer input itself.
 */
enum class UpscaleX11Presentation {
    None,
    Xwayland,
    Effect,
};

enum class UpscaleRefusal {
    None,

    Disabled,
    Unlisted,
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
    NotActive,
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
    BufferNoWholeFactor,
    BufferFilterRange,
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

/** Which of the six cells this window presents in. */
UpscalePresentation upscalePresentationOf(EffectWindow *window);

/** Fullscreen, or a profiled borderless window covering its own output or drawn over it. */
bool upscalePresentation(EffectWindow *window);

/**
 * Tells eligibility the screen mode, in device pixels, the effect told the
 * Wayland client of a window about that window's output, or an invalid size.
 * Set by the effect, which keeps what it told; unset, nothing was told.
 */
void upscaleSetToldMode(std::function<QSize(const EffectWindow *window)> told);

/** A smaller fullscreen configure held by Auto and presented over its output. */
void upscaleSetResizedGeometry(std::function<QRectF(const Window *window)> requested);

/**
 * Whether @p window is a plain Wayland window its program sized to the smaller
 * screen mode it was told, rather than going fullscreen, as GLFW 3.4 with an
 * undecorated window at the video mode's size and Wine's Wayland driver do.
 * Such a window is drawn enlarged over its whole output, without any
 * decoration KWin gave it, with the pointer mapped from there onto it, as Jens
 * decided on 2026-09-29.
 */
bool upscaleDrawnOverOutput(const EffectWindow *window);

/**
 * The rectangle a presented window's picture is laid in: its output for one
 * drawn over it, its frame otherwise.
 */
UpscaleRectF upscalePresentedFrame(const EffectWindow *window);

/**
 * Whether @p window lies under the picture of @p drawn, a window drawn over
 * its output: below it, or above it in a layer an active fullscreen window
 * would cover, while @p drawn is the active window. So the picture covers
 * what KWin would let a fullscreen window cover, and nothing more.
 */
bool upscaleDrawnCovers(EffectWindow *drawn, EffectWindow *window);

/**
 * Whether the window's frame sits exactly on its output, in device pixels.
 *
 * Fullscreen is a state, not a size. A client can hold that state while its
 * window is still being sized during startup, and a window this effect has
 * itself made smaller keeps the state too. Anything that acts on a window
 * because it presents full-screen has to ask this as well, or it acts on a
 * window that presents nothing of the kind.
 */
bool upscaleCoversOutput(const EffectWindow *window);

/**
 * Whether the geometry KWin has asked the window to take still covers the
 * output it is moving to. A committed geometry lags the request by a
 * configure: a window leaving fullscreen still covers its output until its
 * client answers, and whatever is taken back only then is sent to it in a
 * configure of its own, after the one that already told it its new size.
 */
bool upscaleRequestCoversOutput(const Window *window);

/**
 * Why this window cannot be scaled, or None when only the effect's own state
 * or another window can still refuse it.
 *
 * This runs for every window of every frame, so it answers with the reason
 * alone and allocates nothing; the description is built only when something
 * asks for one.
 */
UpscaleRefusal windowRefusal(EffectWindow *window);

/**
 * The surface whose picture @p window shows: its own, or the one subsurface
 * covering it whole, above the window's own buffer, which Wine's Wayland
 * driver draws a game into. Null without a surface.
 */
SurfaceItem *upscalePictureSurface(const EffectWindow *window);

/**
 * Where @p window's supplied buffer goes on its output, in device pixels, as
 * its settings lay it: the one answer eligibility, drawing and input share.
 */
UpscalePicture upscalePictureOf(EffectWindow *window);

/**
 * The window this output would scale once its client supplied a smaller
 * buffer, or null.
 *
 * Everything about the window has to hold - it presents full screen or
 * borderless over its output, the settings reach it, nothing covers or
 * transforms it - and only its surface and buffer may still stand in the
 * way. That is the window Auto asks for a smaller buffer: asking only a
 * window that is already scaled would never ask one that draws at full size,
 * which is the one Auto exists for. Null when none, or more than one, does.
 */
EffectWindow *upscaleWindowAwaitingBuffer(UpscaleOutput *output);

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

/** Whether the supplied buffer came from the GPU or through main memory. */
UpscaleBufferKind suppliedBufferKind(SurfaceItem *surface);

} // namespace KWin
