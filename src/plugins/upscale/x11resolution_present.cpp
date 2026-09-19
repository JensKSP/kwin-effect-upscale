/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Who enlarges a window this effect made smaller than its output, and what
// that means for the pointer. Xwayland does it for a client that established
// an emulated mode on its own connection; for every other client the effect
// does it here, and UpscaleX11Input maps the pointer to match.

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "x11geometry.h"
#include "x11input.h"

#include "effect/effectwindow.h"
#include "main.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "x11window.h"

#include <cmath>
#endif

namespace KWin
{

static SurfaceItem *surfaceItem(X11Window *window)
{
    EffectWindow *effectWindow = window ? window->effectWindow() : nullptr;
    return effectWindow && effectWindow->windowItem() ? effectWindow->windowItem()->surfaceItem() : nullptr;
}

// Whether the surface is shown at the frame's size, which is what makes KWin
// paint, damage and clip the window as covering its output. Xwayland's
// viewport does that for an emulated mode; present() does it otherwise.
static bool fillsFrame(X11Window *window, SurfaceItem *surface)
{
    const QSizeF frame = window->frameGeometry().size();
    const QSizeF destination = surface->destinationSize();
    return !frame.isEmpty() && std::abs((destination.width() / frame.width()) - 1) < 1e-6
        && std::abs((destination.height() / frame.height()) - 1) < 1e-6;
}

// How much larger than the client's window the frame is, in the pixels both
// are counted in, as the factor pointer coordinates have to shrink by. One
// while nothing has to: while Xwayland presents the window and scales the
// coordinates itself, and while the buffer is not yet the requested size.
static QPointF presentationScale(const UpscaleX11Resolution::Request &request, QPointF *origin)
{
    X11Window *window = request.window;
    SurfaceItem *surface = surfaceItem(window);
    if (!request.presentedByEffect || !surface || surface->bufferSize() != request.size || window->frameGeometry().isEmpty()) {
        return QPointF(1, 1);
    }
    const QSizeF frame = window->frameGeometry().size() * kwinApp()->xwaylandScale();
    if (origin) {
        *origin = window->bufferGeometry().topLeft();
    }
    return QPointF(request.size.width() / frame.width(), request.size.height() / frame.height());
}

UpscaleX11Presentation UpscaleX11Resolution::presentation(const Window *window) const
{
#if KWIN_BUILD_X11
    for (auto request = m_requests.cbegin(); request != m_requests.cend(); ++request) {
        if (request.key() != window) {
            continue;
        }
        SurfaceItem *surface = surfaceItem(request.key());
        if (!surface || surface->bufferSize() != request->size || !fillsFrame(request.key(), surface)) {
            return UpscaleX11Presentation::None;
        }
        return request->presentedByEffect ? UpscaleX11Presentation::Effect : UpscaleX11Presentation::Xwayland;
    }
#else
    Q_UNUSED(window)
#endif
    return UpscaleX11Presentation::None;
}

#if KWIN_BUILD_X11
// Called on every commit of a window under a request. When the requested
// buffer has arrived and the client established no emulated mode, the surface
// item is sized to the frame here, which is what Xwayland's viewport would
// have done: KWin then paints the window as covering its output, damages
// the whole frame when the buffer changes, and hands the effect's paint pass
// a region that is not clipped to the surface's own rectangle. Measured on
// 2026-09-19: without this the scaler painted the enlarged image, and KWin
// showed only the top-left 2560 x 1440 pixels of it, because
// WorkspaceScene::paintSimpleScreen intersects a window's paint region with
// its item's bounding rectangle.
//
// The client's own resize to normal geometry ends this by itself: a buffer of
// another size makes KWin recompute the destination from the surface again.
void UpscaleX11Resolution::present(X11Window *window)
{
    const auto request = m_requests.find(window);
    SurfaceItem *surface = surfaceItem(window);
    if (request == m_requests.end() || !surface || surface->bufferSize() != request->size) {
        return;
    }
    if (!request->presentedByEffect) {
        if (fillsFrame(window, surface) || upscaleX11ModeMatches(window, request->position, request->size)) {
            return;
        }
        request->presentedByEffect = true;
    }
    if (!fillsFrame(window, surface)) {
        surface->setDestinationSize(window->frameGeometry().size());
        m_input->refresh();
    }
}
#endif

QPointF UpscaleX11Resolution::presentedUnder(const QPointF &position, SurfaceInterface **surface, QPointF *origin) const
{
#if KWIN_BUILD_X11
    for (auto request = m_requests.cbegin(); request != m_requests.cend(); ++request) {
        X11Window *window = request.key();
        if (window->isDeleted() || !window->surface() || !window->frameGeometry().contains(position)) {
            continue;
        }
        const QPointF scale = presentationScale(request.value(), origin);
        if (scale != QPointF(1, 1)) {
            *surface = window->surface();
            return scale;
        }
    }
#else
    Q_UNUSED(position)
    Q_UNUSED(origin)
#endif
    *surface = nullptr;
    return QPointF(1, 1);
}

} // namespace KWin
