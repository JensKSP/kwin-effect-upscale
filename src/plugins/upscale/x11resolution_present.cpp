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
#include "windowidentity.h"
#include "x11geometry.h"
#include "x11input.h"

#include "effect/effectwindow.h"
#include "main.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "wayland/surface.h"
#include "x11window.h"

#include <QLoggingCategory>

#include <cmath>
#include <type_traits>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)
#endif

namespace KWin
{

// The X11 types these helpers take are declared only where X11 support is
// built, and so is the request they read, so they cannot exist without it.
#if KWIN_BUILD_X11
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

template<typename Region>
static bool isBufferRectangle(const Region &input, const QRectF &buffer)
{
    if constexpr (std::is_same_v<Region, QRegion>) {
        return input == QRegion(buffer.toAlignedRect());
    } else {
        return input == Region(buffer);
    }
}

// Xwayland's viewport scales coordinates, but an explicit X11 input shape
// can remain at the drawable's size. Repair coverage only for that complete
// rectangle: empty, inset or nonrectangular shapes may be intentional. This
// reads committed state only; pointer events must not make X11 round trips.
static bool emulatedInputCoverage(const UpscaleX11Resolution::Request &request, UpscalePresentedPointer *presented)
{
    X11Window *window = request.window;
    SurfaceItem *item = surfaceItem(window);
    SurfaceInterface *surface = window->surface();
    const QSizeF frame = window->frameGeometry().size();
    if (request.presentedByEffect || !item || !surface || item->bufferSize() != request.size
        || surface->size() != frame || !fillsFrame(window, item)) {
        return false;
    }
    const QRectF buffer(QPointF(), QSizeF(request.size) / kwinApp()->xwaylandScale());
    // Requests pass upscaleWantedSize()/canUpscale(), which require both
    // dimensions to be smaller and preserve aspect ratio. One-axis or native
    // sizes are outside that contract and must not acquire input intervention.
    if (buffer.width() >= frame.width() || buffer.height() >= frame.height()
        || !isBufferRectangle(surface->input(), buffer)) {
        return false;
    }
    presented->origin = window->bufferGeometry().topLeft();
    presented->client = buffer.translated(presented->origin);
    // Xwayland already maps absolute and relative motion into the drawable.
    // Only focus and click ownership need help; leave their scale at one.
    return true;
}

// How much larger than the client's window the frame is, in the pixels both
// are counted in, as the factor pointer coordinates have to shrink by. One
// while nothing has to: while Xwayland presents the window and scales the
// coordinates itself, and while the buffer is not yet the requested size.
static QPointF presentationScale(const UpscaleX11Resolution::Request &request, QPointF *origin, QRectF *client)
{
    X11Window *window = request.window;
    SurfaceItem *surface = surfaceItem(window);
    if (!request.presentedByEffect || !surface || surface->bufferSize() != request.size || window->frameGeometry().isEmpty()) {
        return QPointF(1, 1);
    }
    const qreal scale = kwinApp()->xwaylandScale();
    const QSizeF frame = window->frameGeometry().size() * scale;
    if (origin) {
        *origin = window->bufferGeometry().topLeft();
    }
    if (client) {
        // The window's own size on the output, in the pixels the output is
        // counted in: what the client asked KWin for, not what it is shown as.
        *client = QRectF(window->bufferGeometry().topLeft(), QSizeF(request.size.width() / scale, request.size.height() / scale));
    }
    return QPointF(request.size.width() / frame.width(), request.size.height() / frame.height());
}
#endif

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
        // A measured client may need its mode as acknowledgement of the
        // resize. Presenting its smaller drawable ourselves can hide a stale
        // viewport inside the game; let validation retry the missing answer.
        const UpscaleApplication *application = upscaleApplicationForWindow(window);
        if (application && application->x11RequiresEmulatedMode) {
            return;
        }
        if (fillsFrame(window, surface) || upscaleX11ModeMatches(window, request->position, request->size)) {
            return;
        }
        request->presentedByEffect = true;
        qCInfo(KWIN_UPSCALE) << "X11 presentation taken by effect:" << request->key << "window" << window->window()
                             << "buffer" << surface->bufferSize() << "previous presentation" << surface->destinationSize()
                             << "frame" << window->frameGeometry() << "pointer scale" << presentationScale(*request, nullptr, nullptr);
    }
    if (!fillsFrame(window, surface)) {
        surface->setDestinationSize(window->frameGeometry().size());
        m_input->refresh();
    }
}
#endif

UpscalePresentedPointer UpscaleX11Resolution::presentedUnder(const QPointF &position) const
{
#if KWIN_BUILD_X11
    for (auto request = m_requests.cbegin(); request != m_requests.cend(); ++request) {
        X11Window *window = request.key();
        if (window->isDeleted() || !window->surface() || !window->frameGeometry().contains(position)) {
            continue;
        }
        UpscalePresentedPointer presented;
        presented.scale = presentationScale(request.value(), &presented.origin, &presented.client);
        if (presented.scale != QPointF(1, 1) || emulatedInputCoverage(request.value(), &presented)) {
            presented.window = window;
            presented.surface = window->surface();
            return presented;
        }
    }
#else
    Q_UNUSED(position)
#endif
    return {};
}

} // namespace KWin
