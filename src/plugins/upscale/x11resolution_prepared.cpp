/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Presenting a window whose program a helper prepared to render smaller; see
// UpscaleX11Resolution::presentPrepared() and UpscalePreparation.

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "x11geometry.h"

#include "effect/effectwindow.h"
#include "main.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "x11window.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)
#endif

namespace KWin
{

void UpscaleX11Resolution::presentPrepared(EffectWindow *effectWindow, const QSize &size)
{
#if KWIN_BUILD_X11
    auto window = qobject_cast<X11Window *>(effectWindow->window());
    if (!window || window->isDeleted() || size.isEmpty()) {
        return;
    }
    qCInfo(KWIN_UPSCALE) << "Prepared window: awaiting buffer" << window->window() << "pid" << window->pid() << "size" << size;
    m_prepared.insert(window, size);
    watch(effectWindow);
    pinPrepared(window);
#else
    Q_UNUSED(effectWindow)
    Q_UNUSED(size)
#endif
}

void UpscaleX11Resolution::setUnfollowed(std::function<void(EffectWindow *window, const QSize &size)> unfollowed)
{
#if KWIN_BUILD_X11
    m_unfollowed = std::move(unfollowed);
#else
    Q_UNUSED(unfollowed)
#endif
}

#if KWIN_BUILD_X11
void UpscaleX11Resolution::pinPrepared(X11Window *window)
{
    const QSize size = m_prepared.value(window);
    WindowItem *item = window->effectWindow() ? window->effectWindow()->windowItem() : nullptr;
    SurfaceItem *surface = item ? item->surfaceItem() : nullptr;
    // Its buffer shows that the program renders at that size in this window;
    // until it does, nothing is changed.
    if (!m_enabled || m_restoring || !surface || surface->bufferSize() != size || !window->output() || !window->isNormalWindow()) {
        return;
    }
    const qreal scale = kwinApp()->xwaylandScale();
    const QPoint position(qRound(window->output()->geometryF().x() * scale), qRound(window->output()->geometryF().y() * scale));
    const QString key = keyFor(window);
    qCInfo(KWIN_UPSCALE) << "Prepared buffer observed: presenting" << window->window() << "pid" << window->pid() << "size" << size;
    // Answered already, and presented by this effect from the start: there is
    // no emulated mode to wait for and nothing to validate against one.
    m_requests.insert(window, {window, key, position, size, false, true, true, {}});
    m_requested.insert(key, size);
    // The same sequence a client's own fullscreen request takes; see
    // fullscreenRequest().
    window->blockGeometryUpdates();
    window->setFullScreen(true);
    window->unblockGeometryUpdates();
    if (!window->isFullScreen()) {
        m_prepared.remove(window);
        restore(window);
        return;
    }
    upscaleX11Configure(window, position, size);
    present(window);
}

// A prepared window is not negotiated with: its program already renders at
// the size it is held at. Its user taking it out of fullscreen gives it back
// to them for good.
bool UpscaleX11Resolution::applyPrepared(X11Window *window)
{
    if (!m_prepared.contains(window)) {
        return false;
    }
    if (m_requests.contains(window) && !window->isFullScreen()) {
        m_prepared.remove(window);
        restore(window);
    }
    return true;
}

// A prepared window was made fullscreen here, not by its client, so it is
// given back as it was before, and at the size it had then.
void UpscaleX11Resolution::unpinPrepared(X11Window *window)
{
    if (m_prepared.contains(window) && window->isFullScreen()) {
        window->setFullScreen(false);
    }
}
#endif

} // namespace KWin
