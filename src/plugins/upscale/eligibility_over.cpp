/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A plain window its program sized to the smaller screen it was told, which
// the effect draws over its whole output as though it were fullscreen.

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
}

void upscaleSetToldMode(std::function<QSize(const EffectWindow *window)> told)
{
    toldMode = std::move(told);
}

bool upscaleDrawnOverOutput(const EffectWindow *window)
{
    const Window *internal = window->window();
    UpscaleOutput *screen = window->screen();
    if (!toldMode || !screen || !window->isWaylandClient() || window->isFullScreen() || !internal || !internal->isNormalWindow()) {
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
    return upscaleSamePixel(client.width(), told.width() / scale, scale) && upscaleSamePixel(client.height(), told.height() / scale, scale)
        && output.contains(client);
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
