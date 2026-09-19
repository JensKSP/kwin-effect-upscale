/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "eligibility.h"
#include "x11geometry.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "x11window.h"

#include <utility>

namespace KWin
{

void UpscaleX11Resolution::forget(X11Window *window)
{
    if (!m_watched.remove(window)) {
        return;
    }
    m_requests.remove(window);
    m_scheduled.remove(window);
    m_waitingForBuffer.remove(window);
    // SFML can destroy one XID before mapping its replacement. Retain the
    // negotiation budget briefly, but never cache a departed PID indefinitely.
    m_expiration.start();
}

void UpscaleX11Resolution::expireState()
{
    QSet<QString> live;
    for (X11Window *window : std::as_const(m_watched)) {
        live.insert(keyFor(window));
    }
    const auto orphaned = [&live](const auto &entry) {
        return !live.contains(entry.key());
    };
    m_requested.removeIf(orphaned);
    m_failures.removeIf(orphaned);
    m_attempts.removeIf(orphaned);
    m_retries.removeIf(orphaned);
    m_validation.removeIf(orphaned);
    // Validation revisions are unique across keys and expiry, so a delayed
    // callback cannot validate a later launch that happens to reuse a PID.
}

static X11Window *findWindow(xcb_window_t identifier)
{
    EffectWindow *window = effects->findWindow(WId(identifier));
    return window ? qobject_cast<X11Window *>(window->window()) : nullptr;
}

bool UpscaleX11Resolution::fullscreenRequest(X11Window *window, xcb_client_message_event_t *message)
{
    if (keyFor(window).isEmpty()) {
        return false;
    }
    if (m_stateAtom == XCB_ATOM_NONE) {
        m_stateAtom = Xcb::Atom(QByteArrayLiteral("_NET_WM_STATE"));
        m_fullscreenAtom = Xcb::Atom(QByteArrayLiteral("_NET_WM_STATE_FULLSCREEN"));
    }
    if (message->type != m_stateAtom || message->format != 32
        || message->data.data32[0] > 2) {
        return false;
    }
    const bool first = message->data.data32[1] == m_fullscreenAtom;
    const bool second = message->data.data32[2] == m_fullscreenAtom;
    if (!first && !second) {
        return false;
    }
    const bool fullscreen = message->data.data32[0] == 1
        || (message->data.data32[0] == 2 && !window->isFullScreen());
    if (!fullscreen) {
        restore(window);
        return false;
    }
    const Request request = requestFor(window);
    if (!request.window || (!m_requests.contains(window) && !upscaleX11ModeMatches(window, request.position, request.size))) {
        return false;
    }
    if (!begin(request)) {
        return false;
    }
    // Suppress only the full-output native configure, not KWin's fullscreen
    // state, restore geometry or stacking policy. The no-argument unblock does
    // not flush a configure; blockGeometryUpdates(false) would undo the request.
    // moveResizeInternal still updates KWin's logical frame while blocked;
    // it guards only the native configure, not the logical geometry assignment.
    window->blockGeometryUpdates();
    window->setFullScreen(true);
    window->unblockGeometryUpdates();
    if (!window->isFullScreen()) {
        restore(window);
        return false;
    }
    upscaleX11Configure(window, request.position, request.size);
    // EWMH permits two state atoms. Let KWin process the other atom normally;
    // applying or swallowing the entire message would corrupt unrelated state.
    if (first) {
        message->data.data32[1] = XCB_ATOM_NONE;
    }
    if (second) {
        message->data.data32[2] = XCB_ATOM_NONE;
    }
    return false;
}

bool UpscaleX11Resolution::event(xcb_generic_event_t *generic)
{
    if (!m_enabled || m_restoring) {
        return false;
    }
    const uint8_t type = generic->response_type & ~0x80;
    if (type == XCB_CLIENT_MESSAGE) {
        auto message = reinterpret_cast<xcb_client_message_event_t *>(generic);
        X11Window *window = findWindow(message->window);
        return window && !window->isDeleted() ? fullscreenRequest(window, message) : false;
    }
    auto configure = reinterpret_cast<xcb_configure_request_event_t *>(generic);
    X11Window *window = findWindow(configure->window);
    const auto request = m_requests.constFind(window);
    if (!window || window->isDeleted() || !upscalePresentation(window->effectWindow()) || request == m_requests.cend()) {
        return false;
    }
    if (configure->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        window->restackWindow(configure->sibling, configure->stack_mode, NET::FromApplication, window->userTime());
    }
    upscaleX11Configure(window, request->position, request->size, true);
    return true;
}

} // namespace KWin
#endif
