/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "matching.h"
#include "runtime.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "effect/xcb.h"
#include "main.h"
#include "utils/executable_path.h"
#include "x11window.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QTimer>

#include <cstring>
#include <optional>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

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
    const QString executable = info.pid() > 0 ? executablePathFromPid(info.pid()) : QString();
    // Wine obtains its screen from its prefix at startup. Resizing its running
    // window fights that screen and flickers; preparation owns this path.
    if (upscaleWineRuntime(executable)) {
        return false;
    }
    const UpscaleApplication *application = upscaleApplicationFor({executable,
                                                                   QString::fromLatin1(info.windowClassClass()), QString::fromLatin1(info.windowClassName())});
    const UpscaleSettings settings = upscaleResolveSettings(application);
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
