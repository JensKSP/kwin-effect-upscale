/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11geometry.h"

#if KWIN_BUILD_X11
#include "main.h"
#include "utils/c_ptr.h"
#include "x11window.h"

#include <array>
#include <cstring>
#include <xcb/randr.h>

namespace KWin
{

QPoint upscaleX11Position(const X11Window *window)
{
    const qreal scale = kwinApp()->xwaylandScale();
    return QPoint(qRound(window->frameGeometry().x() * scale), qRound(window->frameGeometry().y() * scale));
}

QSize upscaleX11NormalSize(const X11Window *window)
{
    const qreal scale = kwinApp()->xwaylandScale();
    return QSize(qRound(window->frameGeometry().width() * scale), qRound(window->frameGeometry().height() * scale));
}

bool upscaleX11PrimaryOutput(const QPoint &position)
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    const auto primary = UniqueCPtr<xcb_randr_get_output_primary_reply_t>(xcb_randr_get_output_primary_reply(connection,
                                                                                                             xcb_randr_get_output_primary(connection, kwinApp()->x11RootWindow()), nullptr));
    if (!primary || primary->output == XCB_NONE) {
        return false;
    }
    const auto output = UniqueCPtr<xcb_randr_get_output_info_reply_t>(xcb_randr_get_output_info_reply(connection,
                                                                                                      xcb_randr_get_output_info(connection, primary->output, XCB_CURRENT_TIME), nullptr));
    if (!output || output->crtc == XCB_NONE) {
        return false;
    }
    const auto crtc = UniqueCPtr<xcb_randr_get_crtc_info_reply_t>(xcb_randr_get_crtc_info_reply(connection,
                                                                                                xcb_randr_get_crtc_info(connection, output->crtc, XCB_CURRENT_TIME), nullptr));
    return crtc && QPoint(crtc->x, crtc->y) == position;
}

static bool outputHasMode(xcb_randr_get_screen_resources_current_reply_t *resources,
                          xcb_randr_get_output_info_reply_t *output, const QSize &size)
{
    const auto modes = xcb_randr_get_screen_resources_current_modes(resources);
    const auto outputModes = xcb_randr_get_output_info_modes(output);
    for (int candidate = 0; candidate < output->num_modes; ++candidate) {
        for (int mode = 0; mode < resources->num_modes; ++mode) {
            if (modes[mode].id == outputModes[candidate] && QSize(modes[mode].width, modes[mode].height) == size) {
                return true;
            }
        }
    }
    return false;
}

bool upscaleX11ModeAvailable(const QPoint &position, const QSize &size)
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    const auto resources = UniqueCPtr<xcb_randr_get_screen_resources_current_reply_t>(xcb_randr_get_screen_resources_current_reply(connection,
                                                                                                                                   xcb_randr_get_screen_resources_current(connection, kwinApp()->x11RootWindow()), nullptr));
    if (!resources) {
        return false;
    }
    const auto outputs = xcb_randr_get_screen_resources_current_outputs(resources.get());
    for (int index = 0; index < resources->num_outputs; ++index) {
        const auto output = UniqueCPtr<xcb_randr_get_output_info_reply_t>(xcb_randr_get_output_info_reply(connection,
                                                                                                          xcb_randr_get_output_info(connection, outputs[index], resources->config_timestamp), nullptr));
        if (!output || output->crtc == XCB_NONE) {
            continue;
        }
        const auto crtc = UniqueCPtr<xcb_randr_get_crtc_info_reply_t>(xcb_randr_get_crtc_info_reply(connection,
                                                                                                    xcb_randr_get_crtc_info(connection, output->crtc, resources->config_timestamp), nullptr));
        if (!crtc || QPoint(crtc->x, crtc->y) != position) {
            continue;
        }
        if (outputHasMode(resources.get(), output.get(), size)) {
            return true;
        }
    }
    return false;
}

bool upscaleX11ModeMatches(X11Window *window, const QPoint &position, const QSize &size)
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    constexpr char name[] = "_XWAYLAND_RANDR_EMU_MONITOR_RECTS";
    const auto atom = UniqueCPtr<xcb_intern_atom_reply_t>(xcb_intern_atom_reply(connection,
                                                                                xcb_intern_atom(connection, true, sizeof(name) - 1, name), nullptr));
    if (!atom || atom->atom == XCB_ATOM_NONE) {
        return false;
    }
    const auto property = UniqueCPtr<xcb_get_property_reply_t>(xcb_get_property_reply(connection,
                                                                                      xcb_get_property(connection, false, window->window(), atom->atom, XCB_ATOM_CARDINAL, 0, 1024), nullptr));
    if (!property || property->format != 32 || property->value_len % 4 != 0 || property->bytes_after != 0) {
        return false;
    }
    const auto values = static_cast<const uint32_t *>(xcb_get_property_value(property.get()));
    for (uint32_t index = 0; index < property->value_len; index += 4) {
        if (values[index] == uint32_t(position.x()) && values[index + 1] == uint32_t(position.y())
            && values[index + 2] == uint32_t(size.width()) && values[index + 3] == uint32_t(size.height())) {
            return true;
        }
    }
    return false;
}

// Recent KWin manages the application window directly. Older KWin reparents
// it into a wrapper and frame; every member of that hierarchy must agree or
// Xwayland cannot recognize a fullscreen emulated mode at the output origin.
template<typename WindowType>
static void configureHierarchy(WindowType *window, const QPoint &position, const QSize &size)
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    constexpr uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t frame[] = {uint32_t(position.x()), uint32_t(position.y()), uint32_t(size.width()), uint32_t(size.height())};
    if constexpr (requires { window->frameId(); window->wrapperId(); }) {
        const uint32_t child[] = {0, 0, uint32_t(size.width()), uint32_t(size.height())};
        xcb_configure_window(connection, window->frameId(), mask, frame);
        xcb_configure_window(connection, window->wrapperId(), mask, child);
        xcb_configure_window(connection, window->window(), mask, child);
    } else {
        xcb_configure_window(connection, window->window(), mask, frame);
    }
}

void upscaleX11Configure(X11Window *window, const QPoint &position, const QSize &size, bool notify)
{
    configureHierarchy(window, position, size);
    if (notify) {
        // A refused ConfigureRequest still requires the actual client geometry
        // in root coordinates (ICCCM 4.1.5). KWin's normal reply describes its
        // logical fullscreen frame, which is deliberately a different size.
        xcb_configure_notify_event_t event{};
        event.response_type = XCB_CONFIGURE_NOTIFY;
        event.event = window->window();
        event.window = window->window();
        event.x = int16_t(position.x());
        event.y = int16_t(position.y());
        event.width = size.width();
        event.height = size.height();
        // SendEvent always reads 32 wire bytes; XCB's typed ConfigureNotify
        // structure is only 28 bytes and does not include the trailing padding.
        std::array<char, 32> wire{};
        static_assert(sizeof(event) <= wire.size());
        std::memcpy(wire.data(), &event, sizeof(event));
        xcb_send_event(kwinApp()->x11Connection(), false, window->window(), XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                       wire.data());
    }
    xcb_flush(kwinApp()->x11Connection());
}

} // namespace KWin
#endif
