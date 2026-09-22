/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the X server tells this control about the windows it has touched, and
// the waits on a client that those events end.
//
// The waits exist because of how KWin 6.6 treats an X11 window whose client
// emulates a mode through Xwayland. Xwayland records that mode on the client's
// windows, in _XWAYLAND_RANDR_EMU_MONITOR_RECTS, and KWin answers each change
// of the property by sizing the client window to what the property says at
// that moment: the mode it names, or the whole frame once it has gone. The
// property, then, is the client's word on how large its window is, and KWin
// enforces it. This control sizes the window directly, behind that. Whatever
// it asks holds only while the client's word agrees with it, so nothing is
// asked of a window while a change of that word is still on its way.

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "eligibility.h"
#include "x11geometry.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "x11window.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

void UpscaleX11Resolution::forget(X11Window *window)
{
    if (!m_watched.remove(window)) {
        return;
    }
    m_requests.remove(window);
    m_prepared.remove(window);
    m_scheduled.remove(window);
    m_waitingForBuffer.remove(window);
    m_withdrawals.remove(window);
    m_overdue.remove(window);
    m_releases.remove(window);
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

// Gives the window back once its client has answered the request, or once
// the client has had the whole validation window to answer it.
//
// A client answers a request by establishing the emulated mode it names, a
// few round trips after it has read the resize. Restoring the window before
// that answer arrives lets the answer overtake the restore: KWin then sizes
// the window to the mode of a request that no longer exists, and every
// request made in the meantime is undone by it. Measured on KWin 6.6.6 on
// 2026-09-21: a reconfiguration 56 ms after a request found no mode yet,
// restored and asked again, and the client's three answers - the old mode,
// its withdrawal, the new mode - arrived within one millisecond; KWin read
// the property while it was withdrawn and sized the client to the whole
// frame, undoing the new request until validation retried it three seconds
// later. A client that never answers, one that supplies the buffer without
// selecting a mode, is released when validation would have judged it, which
// is when this control stops expecting an answer anyway. The token keeps an
// earlier release's timer from ending a later release of the same window.
void UpscaleX11Resolution::release(X11Window *window)
{
    const auto request = m_requests.find(window);
    if (request == m_requests.end() || window->isDeleted()) {
        restore(window);
        return;
    }
    // Read now as well as noted as it arrives: the answer may have come
    // before anything was waiting for it.
    request->answered = request->answered || upscaleX11ModeMatches(window, request->position, request->size);
    if (request->answered) {
        restore(window);
        return;
    }
    const int token = ++m_nextWait;
    m_releases.insert(window, token);
    const QPointer<X11Window> guarded = window;
    QTimer::singleShot(int(request->verdict.remainingTime()), this, [this, guarded, token]() {
        if (guarded && m_releases.value(guarded) == token) {
            restore(guarded);
            schedule(guarded);
        }
    });
}

// The client holds an emulated mode this effect has just asked it to give up,
// or one of its own: nothing more is asked of it until the property Xwayland
// keeps for it changes. Not indefinitely, because a client that keeps a mode
// its window no longer has never withdraws it: after the validation window
// the request is made regardless, and validation says what became of it. The
// token keeps an earlier wait's timer from ending a later wait on the same
// window.
void UpscaleX11Resolution::awaitWithdrawal(X11Window *window)
{
    const int token = ++m_nextWait;
    m_withdrawals.insert(window, token);
    m_overdue.remove(window);
    qCDebug(KWIN_UPSCALE) << "Waiting for" << keyFor(window) << "to withdraw its emulated mode";
    const QPointer<X11Window> guarded = window;
    QTimer::singleShot(s_validationWindow, this, [this, guarded, token]() {
        if (guarded && m_withdrawals.value(guarded) == token) {
            m_withdrawals.remove(guarded);
            m_overdue.insert(guarded);
            schedule(guarded);
        }
    });
}

// Xwayland has changed the emulated mode it records for this window's client.
// That is the answer a window in m_releases waits for, when the mode is the
// one its request named, and the change a window in m_withdrawals waits for,
// whatever the mode became: whether the client withdrew its mode or chose
// another is apply()'s to read afterwards.
void UpscaleX11Resolution::emulatedModeChanged(xcb_property_notify_event_t *property)
{
    if (m_requests.isEmpty() && m_withdrawals.isEmpty()) {
        return;
    }
    if (m_emulationAtom == XCB_ATOM_NONE) {
        m_emulationAtom = Xcb::Atom(QByteArrayLiteral("_XWAYLAND_RANDR_EMU_MONITOR_RECTS"));
    }
    if (property->atom != m_emulationAtom) {
        return;
    }
    X11Window *window = findWindow(property->window);
    if (!window) {
        return;
    }
    // KWin handles this event after this filter, and its handling is what
    // sizes the window from the property. Whatever follows the event has to
    // follow that, so it is scheduled for after the event has been
    // dispatched. The token names the wait: a restore between the event and
    // the callback starts a newer wait for the same window, and that one has
    // to survive a callback that belongs to the old one - the fallback timers
    // check the same way. Tokens start at one, so an absent window reads as
    // no wait.
    const QPointer<X11Window> guarded = window;
    const auto request = m_requests.find(window);
    const bool answer = request != m_requests.end() && !request->answered && property->state == XCB_PROPERTY_NEW_VALUE
        && upscaleX11ModeMatches(window, request->position, request->size);
    if (answer) {
        request->answered = true;
    }
    if (answer && m_releases.contains(window)) {
        const int token = m_releases.value(window);
        QTimer::singleShot(0, this, [this, guarded, token]() {
            if (guarded && m_releases.value(guarded) == token) {
                restore(guarded);
                schedule(guarded);
            }
        });
    }
    if (m_withdrawals.contains(window)) {
        const int token = m_withdrawals.value(window);
        QTimer::singleShot(0, this, [this, guarded, token]() {
            if (guarded && m_withdrawals.value(guarded) == token) {
                m_withdrawals.remove(guarded);
                schedule(guarded);
            }
        });
    }
}

bool UpscaleX11Resolution::event(xcb_generic_event_t *generic)
{
    const uint8_t type = generic->response_type & ~0x80;
    if (type == XCB_PROPERTY_NOTIFY) {
        // Bookkeeping about the client, which control being off does not
        // suspend: an answer or a withdrawal that arrives then still has to
        // end its wait, or the wait outlives the reconfiguration that turns
        // control on.
        emulatedModeChanged(reinterpret_cast<xcb_property_notify_event_t *>(generic));
        return false;
    }
    if (m_restoring) {
        return false;
    }
    if (type == XCB_CLIENT_MESSAGE) {
        if (!m_enabled) {
            return false;
        }
        auto message = reinterpret_cast<xcb_client_message_event_t *>(generic);
        X11Window *window = findWindow(message->window);
        return window && !window->isDeleted() ? fullscreenRequest(window, message) : false;
    }
    auto configure = reinterpret_cast<xcb_configure_request_event_t *>(generic);
    X11Window *window = findWindow(configure->window);
    if (!window || window->isDeleted() || !upscalePresentation(window->effectWindow())) {
        return false;
    }
    // A request that stands keeps the window at its size, and a window whose
    // client has yet to withdraw its mode keeps the size it was handed back
    // at. Both are answered here rather than by KWin, because KWin's answer
    // to a ConfigureRequest is a synthetic ConfigureNotify carrying the
    // client geometry it last configured itself - which this control has
    // changed behind it. A client told that stale size may act on it, and on
    // KWin 6.6 a client that selects the mode it was told re-establishes the
    // mode this control had just had it withdraw, for good. Measured on
    // 2026-09-21: a resize request that reached KWin during the withdrawal
    // wait after a preset change was answered with the previous request's
    // size, and the window held that size with no request behind it.
    const auto request = m_requests.constFind(window);
    const bool withdrawing = m_withdrawals.contains(window);
    if (request == m_requests.cend() && !withdrawing) {
        return false;
    }
    if (configure->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        window->restackWindow(configure->sibling, configure->stack_mode, NET::FromApplication, window->userTime());
    }
    if (request != m_requests.cend()) {
        upscaleX11Configure(window, request->position, request->size, true);
    } else {
        upscaleX11Configure(window, upscaleX11Position(window), upscaleX11NormalSize(window), true);
    }
    return true;
}

} // namespace KWin
#endif
