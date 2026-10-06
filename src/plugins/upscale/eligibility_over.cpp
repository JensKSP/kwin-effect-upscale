/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A window sized to the smaller screen it was told, or a fullscreen window
// resized by Auto, which the effect draws over its whole output.

#include "eligibility.h"

#include "resolution.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"

namespace KWin
{

namespace
{
std::function<QSize(const EffectWindow *)> toldMode;
std::function<QRectF(const Window *)> resizedGeometry;
}

void upscaleSetToldMode(std::function<QSize(const EffectWindow *window)> told)
{
    toldMode = std::move(told);
}

void upscaleSetResizedGeometry(std::function<QRectF(const Window *window)> requested)
{
    resizedGeometry = std::move(requested);
}

bool upscaleDrawnOverOutput(const EffectWindow *window)
{
    const Window *internal = window->window();
    UpscaleOutput *screen = window->screen();
    if (!screen || !window->isWaylandClient() || !internal || !internal->isNormalWindow()) {
        return false;
    }
    if (resizedGeometry && window->isFullScreen()) {
        const QRectF requested = resizedGeometry(internal);
        if (!requested.isEmpty() && internal->clientGeometry() == requested && screen->geometryF().contains(requested)) {
            return true;
        }
    }
    if (!toldMode) {
        return false;
    }
    const QSize told = toldMode(window);
    const QSize pixels = screen->pixelSize();
    if (told.isEmpty() || (told.width() >= pixels.width() && told.height() >= pixels.height())) {
        return false;
    }
    // The size the program believes its whole screen to be, wherever KWin
    // placed the window on the real one, which it lies inside. Measured on the
    // window's surface, which is what the program sized: KWin may decorate it
    // all the same, as it does GLFW 3.4's undecorated window when libdecor
    // negotiates the decoration (observed on KWin 6.3.6, 2026-09-29).
    const double scale = screen->scale();
    const auto client = internal->clientGeometry();
    const auto output = screen->geometryF();
    if (upscaleSamePixel(client.width(), told.width() / scale, scale) && upscaleSamePixel(client.height(), told.height() / scale, scale)
        && output.contains(client)) {
        return true;
    }
    // Wine's Wayland driver sizes a window in the Windows pixels of the screen
    // it was told, one to a logical unit unless Wine's own DPI setting says
    // otherwise, whatever the output's scale, and keeps a fullscreen window at
    // that size (Wine 10.0, seen at desktop scale 1.05 on KWin 6.3.6,
    // 2026-10-07). On a scaled desktop that is not the told size in logical
    // units, and can be larger than the output; its picture is the told size
    // all the same, and from the output's corner it is drawn over the output.
    const SurfaceItem *picture = upscalePictureSurface(window);
    return window->isFullScreen() && picture && upscaleSuppliedSize(picture) == told && client.topLeft() == output.topLeft();
}

UpscaleRectF upscalePresentedFrame(const EffectWindow *window)
{
    // Spelled as one type: KWin 6.6 answers the output's geometry in its own
    // rectangle type and the window's in Qt's.
    return upscaleDrawnOverOutput(window) && window->screen() ? UpscaleRectF(window->screen()->geometryF()) : UpscaleRectF(window->frameGeometry());
}

bool upscaleDrawnCovers(EffectWindow *drawn, EffectWindow *window)
{
    if (!drawn || !window || window == drawn || !effects) {
        return false;
    }
    const QList<EffectWindow *> stacking = effects->stackingOrder();
    if (stacking.indexOf(window) < stacking.indexOf(drawn)) {
        return true;
    }
    // KWin raises an active fullscreen window into a layer of its own, above
    // panels, windows kept above and ordinary notifications, and below popups,
    // critical notifications and on-screen displays.
    const Window *internal = window->window();
    return effects->activeWindow() == drawn && internal && internal->layer() < ActiveLayer;
}

} // namespace KWin
