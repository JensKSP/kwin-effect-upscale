/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"

#include <QSocketNotifier>
#include <QTimer>

#include <cstdlib>
#include <memory>
#include <xcb/randr.h>

template<typename T>
using Reply = std::unique_ptr<T, decltype(&std::free)>;

X11Client::X11Client(bool cooperative, int ignoredResizes)
    : m_cooperative(cooperative)
    , m_ignoredResizes(ignoredResizes)
{
    m_connection = xcb_connect(nullptr, nullptr);
    if (!xcb_connection_has_error(m_connection)) {
        m_screen = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data;
        auto notifier = new QSocketNotifier(xcb_get_file_descriptor(m_connection), QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &X11Client::dispatch);
        // Synchronous observation can read events into XCB's queue without
        // leaving the socket readable. Drain those events as well.
        auto timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &X11Client::dispatch);
        timer->start(20);
    }
}

X11Client::~X11Client()
{
    xcb_disconnect(m_connection);
}

xcb_atom_t X11Client::atom(const QByteArray &name) const
{
    const Reply<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(m_connection,
                                                                     xcb_intern_atom(m_connection, false, name.size(), name.constData()), nullptr),
                                               &std::free);
    return reply ? reply->atom : xcb_atom_t(XCB_NONE);
}

bool X11Client::show(const QByteArray &identity, const QRect &geometry, bool full)
{
    if (!m_screen) {
        return false;
    }
    m_position = geometry.topLeft();
    m_size = geometry.size();
    m_fullscreenOnMap = full;
    m_window = xcb_generate_id(m_connection);
    const uint32_t values[] = {0xff0000, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_EXPOSURE};
    xcb_create_window(m_connection, XCB_COPY_FROM_PARENT, m_window, m_screen->root,
                      geometry.x(), geometry.y(), geometry.width(), geometry.height(), 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, m_screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    const QByteArray windowClass = identity + '\0' + identity + '\0';
    xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING, 8, windowClass.size(), windowClass.constData());
    // An explicit position makes the two-output case independent of placement
    // policy. Motif decorations=0 models a normal managed borderless window.
    const uint32_t hints[] = {1U << 1, 0, 0, 0, 0};
    const xcb_atom_t motif = atom(QByteArrayLiteral("_MOTIF_WM_HINTS"));
    xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, motif, motif, 32, 5, hints);
    uint32_t normalHints[18] = {1U, uint32_t(geometry.x()), uint32_t(geometry.y())};
    xcb_change_property(m_connection, XCB_PROP_MODE_REPLACE, m_window, XCB_ATOM_WM_NORMAL_HINTS,
                        XCB_ATOM_WM_SIZE_HINTS, 32, 18, normalHints);
    m_context = xcb_generate_id(m_connection);
    const uint32_t foreground = 0xff0000;
    xcb_create_gc(m_connection, m_context, m_window, XCB_GC_FOREGROUND, &foreground);
    xcb_map_window(m_connection, m_window);
    paint(m_size);
    xcb_flush(m_connection);
    return true;
}

void X11Client::fullscreen(bool enabled)
{
    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = m_window;
    if (enabled) {
        // EWMH uses RandR monitor order, which need not be left-to-right.
        // Match the output by its origin, as KWin's xineramaIndexToOutput
        // resolves the resulting index through this same monitor list.
        const Reply<xcb_randr_get_monitors_reply_t> monitors(xcb_randr_get_monitors_reply(m_connection,
                                                                                          xcb_randr_get_monitors(m_connection, m_screen->root, true), nullptr),
                                                             &std::free);
        if (!monitors) {
            return;
        }
        uint32_t monitor = 0;
        for (auto item = xcb_randr_get_monitors_monitors_iterator(monitors.get()); item.rem; xcb_randr_monitor_info_next(&item)) {
            if (QPoint(item.data->x, item.data->y) == m_position) {
                monitor = monitors->nMonitors - item.rem;
                break;
            }
        }
        event.type = atom(QByteArrayLiteral("_NET_WM_FULLSCREEN_MONITORS"));
        for (int index = 0; index < 4; ++index) {
            event.data.data32[index] = monitor;
        }
        event.data.data32[4] = 1;
        xcb_send_event(m_connection, false, m_screen->root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                       reinterpret_cast<const char *>(&event));
        event.data.data32[4] = 0;
        event.data.data32[2] = 0;
    }
    event.type = atom(QByteArrayLiteral("_NET_WM_STATE"));
    event.data.data32[0] = enabled ? 1 : 0;
    event.data.data32[1] = atom(QByteArrayLiteral("_NET_WM_STATE_FULLSCREEN"));
    event.data.data32[3] = 1;
    xcb_send_event(m_connection, false, m_screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(m_connection);
}

void X11Client::resize(const QSize &size)
{
    const uint32_t values[] = {uint32_t(size.width()), uint32_t(size.height())};
    xcb_configure_window(m_connection, m_window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    xcb_flush(m_connection);
}

QRect X11Client::geometry() const
{
    const Reply<xcb_get_geometry_reply_t> geometry(xcb_get_geometry_reply(m_connection,
                                                                          xcb_get_geometry(m_connection, m_window), nullptr),
                                                   &std::free);
    const Reply<xcb_translate_coordinates_reply_t> position(xcb_translate_coordinates_reply(m_connection,
                                                                                            xcb_translate_coordinates(m_connection, m_window, m_screen->root, 0, 0), nullptr),
                                                            &std::free);
    return geometry && position ? QRect(position->dst_x, position->dst_y, geometry->width, geometry->height) : QRect();
}

bool X11Client::mode(const QSize &size)
{
    const Reply<xcb_randr_get_screen_resources_current_reply_t> resources(xcb_randr_get_screen_resources_current_reply(m_connection,
                                                                                                                       xcb_randr_get_screen_resources_current(m_connection, m_screen->root), nullptr),
                                                                          &std::free);
    if (!resources) {
        return false;
    }
    const xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(resources.get());
    const xcb_randr_mode_info_t *modes = xcb_randr_get_screen_resources_current_modes(resources.get());
    for (int index = 0; index < resources->num_outputs; ++index) {
        const Reply<xcb_randr_get_output_info_reply_t> output(xcb_randr_get_output_info_reply(m_connection,
                                                                                              xcb_randr_get_output_info(m_connection, outputs[index], resources->config_timestamp), nullptr),
                                                              &std::free);
        if (!output || !output->crtc) {
            continue;
        }
        const Reply<xcb_randr_get_crtc_info_reply_t> crtc(xcb_randr_get_crtc_info_reply(m_connection,
                                                                                        xcb_randr_get_crtc_info(m_connection, output->crtc, resources->config_timestamp), nullptr),
                                                          &std::free);
        if (!crtc || QPoint(crtc->x, crtc->y) != m_position) {
            continue;
        }
        for (int candidate = 0; candidate < resources->num_modes; ++candidate) {
            if (QSize(modes[candidate].width, modes[candidate].height) != size) {
                continue;
            }
            const Reply<xcb_randr_set_crtc_config_reply_t> result(xcb_randr_set_crtc_config_reply(m_connection,
                                                                                                  xcb_randr_set_crtc_config(m_connection, output->crtc, XCB_CURRENT_TIME, resources->config_timestamp,
                                                                                                                            crtc->x, crtc->y, modes[candidate].id, crtc->rotation, 1, &outputs[index]),
                                                                                                  nullptr),
                                                                  &std::free);
            return result && result->status == XCB_RANDR_SET_CONFIG_SUCCESS;
        }
    }
    return false;
}

void X11Client::paint(const QSize &size)
{
    const xcb_rectangle_t rectangle{0, 0, uint16_t(size.width()), uint16_t(size.height())};
    xcb_poly_fill_rectangle(m_connection, m_window, m_context, 1, &rectangle);
    xcb_flush(m_connection);
}

void X11Client::dispatch()
{
    while (xcb_generic_event_t *event = xcb_poll_for_event(m_connection)) {
        const uint8_t type = event->response_type & ~0x80;
        if (type == XCB_MAP_NOTIFY && m_fullscreenOnMap) {
            // EWMH messages sent before management can be lost while KWin
            // waits for Xwayland's surface association. Real toolkits wait for
            // mapping before requesting fullscreen for the same reason.
            m_fullscreenOnMap = false;
            fullscreen(true);
        } else if (type == XCB_CONFIGURE_NOTIFY) {
            const auto configure = reinterpret_cast<xcb_configure_notify_event_t *>(event);
            const QSize size(configure->width, configure->height);
            if (size != m_size) {
                m_size = size;
                if (m_ignoredResizes > 0) {
                    --m_ignoredResizes;
                } else if (m_cooperative) {
                    mode(size);
                }
                paint(size);
            }
        } else if (type == XCB_EXPOSE) {
            paint(m_size);
        }
        std::free(event);
    }
}
