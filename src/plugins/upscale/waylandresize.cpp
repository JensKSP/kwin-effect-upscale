/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "waylandscale.h"

#include "eligibility.h"

#include "effect/effectwindow.h"
#include "window.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

bool UpscaleWaylandScale::resize(Window *window, Request &request)
{
    if (!window->isRequestedFullScreen() || !window->output()) {
        return false;
    }
    const QRectF output = window->output()->geometryF();
    const QSize pixels = window->output()->pixelSize();
    request.geometry = QRectF(output.topLeft(), QSizeF(qRound(output.width() * request.ratio), qRound(output.height() * request.ratio)));
    request.pixels = QSize(qRound(pixels.width() * request.ratio), qRound(pixels.height() * request.ratio));
    request.resizing = true;
    request.frames = 0;
    // Set the state first: both changes emit the geometry/scale signals this
    // controller observes. They must see the request being made, not undo it.
    window->setNextTargetScale(request.original);
    qCInfo(KWIN_UPSCALE) << "Wayland resize request: window" << window->internalId() << "geometry" << request.geometry;
    window->moveResize(request.geometry);
    return true;
}

void UpscaleWaylandScale::restoreGeometry(Window *window, const Request &request)
{
    // Leaving fullscreen has already scheduled the application's normal
    // geometry. Restore only a fullscreen rectangle still owned by us.
    if (request.resizing && !window->isDeleted() && window->isRequestedFullScreen() && holdsResize(window, request.geometry)) {
        window->moveResize(window->output()->geometryF());
    }
}

// Whether the geometry KWin holds for the window is still the smaller one
// this effect asked for. Not compared exactly: once the client answers, KWin
// takes the size it committed, which is the request rounded through whole
// buffer pixels. Measured 2026-10-11 with Quake II RTX at scale 1.45: asked
// for 1766 x 993, it committed 1766.21 x 993.10. Compared exactly, the request
// was dropped the moment it succeeded, and the full size was never given back.
bool UpscaleWaylandScale::holdsResize(const Window *window, const QRectF &geometry)
{
    const UpscaleOutput *output = window->output();
    return output && upscaleFrameCovers(window->moveResizeGeometry(), geometry, output->scale());
}

QRectF UpscaleWaylandScale::resizedGeometry(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry == m_requests.constEnd() || !entry->resizing || entry->ignored ? QRectF() : entry->geometry;
}

QSize UpscaleWaylandScale::requestedSize(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry == m_requests.constEnd() || !entry->resizing || entry->ignored ? QSize() : entry->pixels;
}

} // namespace KWin
