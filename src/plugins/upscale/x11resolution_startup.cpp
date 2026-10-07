/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "gamerecognition.h"
#include "matching.h"
#include "runtime.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "effect/xcb.h"
#include "main.h"
#include "utils/executable_path.h"
#include "x11window.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QTimer>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>

#include <xcb/res.h>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

namespace
{

// The process behind a window KWin has not managed yet. A program names itself
// in _NET_WM_PID, which is what KWin 6.3 reads and what nearly every program
// sets. Where it is unset, the connection that made the window says: XRes
// names its client, the session proxy restores that to the program's own, and
// it is where KWin 6.6 takes the process from once it manages the window, so
// the hold and the effect's later view of the window agree.
pid_t mappingProcess(xcb_window_t window, const NETWinInfo &info)
{
    if (info.pid() > 0) {
        return info.pid();
    }
    xcb_connection_t *connection = kwinApp()->x11Connection();
    const xcb_res_client_id_spec_t spec{window, XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID};
    const std::unique_ptr<xcb_res_query_client_ids_reply_t, decltype(&std::free)> reply(
        xcb_res_query_client_ids_reply(connection, xcb_res_query_client_ids(connection, 1, &spec), nullptr), &std::free);
    if (!reply) {
        return 0;
    }
    for (auto ids = xcb_res_query_client_ids_ids_iterator(reply.get()); ids.rem; xcb_res_client_id_value_next(&ids)) {
        if ((ids.data->spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) && xcb_res_client_id_value_value_length(ids.data) > 0) {
            return pid_t(*xcb_res_client_id_value_value(ids.data));
        }
    }
    return 0;
}

// What KWin's own X11 event loop does before it waits again: hand on the
// events XCB has already read (Xwayland::dispatchEvents with EventQueue).
void dispatchQueuedEvents()
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    if (!connection) {
        return;
    }
    while (xcb_generic_event_t *event = xcb_poll_for_queued_event(connection)) {
        qintptr result = 0;
        QCoreApplication::eventDispatcher()->filterNativeEvent(QByteArrayLiteral("xcb_generic_event_t"), event, &result);
        std::free(event);
    }
    xcb_flush(connection);
}

} // namespace

// SFML maps before requesting fullscreen and waits for visibility during
// construction. If KWin exposes the window before handling that request, its
// client can start rendering and discard the subsequent resize during a state
// transition. Hold only selected windows briefly, then apply a queued fullscreen
// request before client round trips can pass the mapping transaction. This is
// not an acknowledgement from the application or control over its renderer.

bool UpscaleX11Resolution::holdMap(xcb_generic_event_t *generic)
{
    const auto &event = *reinterpret_cast<xcb_map_request_event_t *>(generic);
    if (!m_enabled || m_restoring || event.parent != kwinApp()->x11RootWindow()
        || effects->findWindow(WId(event.window))) {
        return false;
    }
    const NETWinInfo info(kwinApp()->x11Connection(), event.window, kwinApp()->x11RootWindow(),
                          NET::WMPid | NET::WMState | NET::WMWindowType, NET::WM2WindowClass);
    const pid_t pid = mappingProcess(event.window, info);
    const QString executable = pid > 0 ? executablePathFromPid(pid) : QString();
    // Wine obtains its screen from its prefix at startup. Resizing its running
    // window fights that screen and flickers, so one whose connection was not
    // answered with a smaller screen is left alone. One whose was already
    // renders at the size wanted and maps like any other program.
    if (upscaleWineRuntime(executable) && !upscaleServed(pid)) {
        return false;
    }
    const UpscaleApplication *application = upscaleApplicationFor({executable,
                                                                   QString::fromLatin1(info.windowClassClass()), QString::fromLatin1(info.windowClassName())});
    UpscaleSettings settings = upscaleResolveSettings(application);
    // All games acts only for a game, as for the window this becomes; see
    // upscaleSettingsForWindow().
    if (!application && settings.acts() && !upscaleServedGame(pid) && !upscaleRecognizedGame(executable)) {
        settings.setActs(false);
    }
    const UpscaleMethod method = upscaleMethodFor(application, UpscalePresentation::X11FullScreen);
    if (!settings.acts() || settings.resolution() == ResolutionPreset::Native
        || (method != UpscaleMethod::Auto && method != UpscaleMethod::X11Resize)
        || (info.windowType(NET::AllTypesMask) != NET::Normal && info.windowType(NET::AllTypesMask) != NET::Unknown)) {
        return false;
    }
    if (m_pendingMaps.contains(event.window)) {
        return true;
    }
    const int token = ++m_nextMap;
    m_pendingMaps.insert(event.window, {*generic, {}, token});
    qCInfo(KWIN_UPSCALE) << "X11 initial mapping held:" << event.window;
    // The client may request fullscreen immediately after MapWindow. Let that
    // request arrive while it is still waiting to become visible. Windowed
    // clients must not wait indefinitely for a request they never make.
    QTimer::singleShot(100, this, [this, id = event.window, token]() {
        const auto pending = m_pendingMaps.constFind(id);
        if (pending != m_pendingMaps.cend() && pending->token == token) {
            mapPending(id);
            // KWin handles a mapping inside its X11 event loop, and that loop
            // goes on to the events the mapping itself caused before control
            // returns, above all the FocusIn that makes the window active,
            // which the round trips of managing it have already read. A
            // mapping released from this timer finishes that loop here.
            // Otherwise the window stays inactive until the loop next runs,
            // and anything that looks at it in between finds it inactive:
            // measured 2026-09-27, 34 of KWin 6.3.6's own X11 window and
            // stacking cases failed while this effect held their windows.
            dispatchQueuedEvents();
        }
    });
    if (info.state().testFlag(NET::FullScreen)) {
        xcb_client_message_event_t full{};
        full.response_type = XCB_CLIENT_MESSAGE;
        full.window = event.window;
        full.format = 32;
        full.type = Xcb::Atom(QByteArrayLiteral("_NET_WM_STATE"));
        full.data.data32[0] = 1;
        full.data.data32[1] = Xcb::Atom(QByteArrayLiteral("_NET_WM_STATE_FULLSCREEN"));
        xcb_generic_event_t message{};
        static_assert(sizeof(full) <= sizeof(message));
        std::memcpy(&message, &full, sizeof(full));
        m_pendingMaps[event.window].messages.append(message);
        mapPending(event.window, true);
    }
    return true;
}

void UpscaleX11Resolution::mapPending(xcb_window_t identifier, bool fullscreen)
{
    if (!m_pendingMaps.contains(identifier)) {
        return;
    }
    PendingMap pending = m_pendingMaps.take(identifier);
    QElapsedTimer duration;
    duration.start();
    {
        // Replay through KWin's public dispatcher so its other event filters
        // still run. Our own filter passes these events through rather than
        // holding their mapping a second time. Keep the complete generic event,
        // including XCB's sequence field, for every filter in that chain.
        const QScopedValueRollback replaying(m_replayingEvents, true);
        // Keep client round trips behind both mapping and the final configure.
        // No event-loop wait or timer runs while the server is grabbed. KWin's
        // guard balances nested grabs as well as early returns.
        std::optional<XServerGrabber> grab;
        if (m_enabled && fullscreen) {
            grab.emplace();
        }
        kwinApp()->dispatchEvent(&pending.event);
        EffectWindow *effectWindow = effects->findWindow(WId(identifier));
        auto window = effectWindow ? qobject_cast<X11Window *>(effectWindow->window()) : nullptr;
        for (auto &message : pending.messages) {
            if (window && m_enabled) {
                fullscreenRequest(window, reinterpret_cast<xcb_client_message_event_t *>(&message));
            }
            kwinApp()->dispatchEvent(&message);
        }
    }
    qCInfo(KWIN_UPSCALE) << "X11 initial mapping released:" << identifier
                         << "queued state requests" << pending.messages.size()
                         << "mapping transaction milliseconds" << duration.elapsed();
}

void UpscaleX11Resolution::flushMaps()
{
    const auto identifiers = m_pendingMaps.keys();
    for (const xcb_window_t identifier : identifiers) {
        mapPending(identifier);
    }
}

bool UpscaleX11Resolution::startupEvent(xcb_generic_event_t *generic)
{
    const uint8_t type = generic->response_type & ~0x80;
    if (type == XCB_MAP_REQUEST) {
        return holdMap(generic);
    }
    if (type == XCB_DESTROY_NOTIFY) {
        m_pendingMaps.remove(reinterpret_cast<xcb_destroy_notify_event_t *>(generic)->window);
        return false;
    }
    if (type == XCB_UNMAP_NOTIFY) {
        // A held window is still unmapped, so a client withdrawing it produces
        // only the synthetic notification ICCCM 4.1.4 sends to the root, and
        // KWin acts on that only for a window it manages. The mapping the
        // client sent first goes first; replayed after the hold instead, it
        // would show a window its client had already withdrawn, such as a
        // toolkit popup shown and hidden at once.
        mapPending(reinterpret_cast<xcb_unmap_notify_event_t *>(generic)->window);
        return false;
    }
    if (type != XCB_CLIENT_MESSAGE) {
        return false;
    }
    const auto message = reinterpret_cast<xcb_client_message_event_t *>(generic);
    const auto pending = m_pendingMaps.find(message->window);
    if (pending == m_pendingMaps.end()) {
        return false;
    }
    pending->messages.append(*generic);
    const Xcb::Atom state(QByteArrayLiteral("_NET_WM_STATE"));
    const Xcb::Atom fullscreen(QByteArrayLiteral("_NET_WM_STATE_FULLSCREEN"));
    if (message->type == state && message->format == 32 && message->data.data32[0] == 1
        && (message->data.data32[1] == fullscreen || message->data.data32[2] == fullscreen)) {
        mapPending(message->window, true);
    }
    return true;
}

} // namespace KWin
#endif
