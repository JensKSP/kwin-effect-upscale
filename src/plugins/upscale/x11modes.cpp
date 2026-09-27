/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "x11geometry.h"
#if KWIN_BUILD_X11
#include "main.h"
#include "utils/c_ptr.h"
#include <QDataStream>
#include <QIODevice>
#include <xcb/randr.h>
namespace KWin
{
static const xcb_randr_mode_info_t *outputMode(xcb_randr_get_screen_resources_current_reply_t *resources,
                                               xcb_randr_get_output_info_reply_t *output, const QSize &size)
{
    const auto modes = xcb_randr_get_screen_resources_current_modes(resources);
    const auto outputModes = xcb_randr_get_output_info_modes(output);
    for (int candidate = 0; candidate < output->num_modes; ++candidate) {
        for (int mode = 0; mode < resources->num_modes; ++mode) {
            if (modes[mode].id == outputModes[candidate] && QSize(modes[mode].width, modes[mode].height) == size) {
                return &modes[mode];
            }
        }
    }
    return nullptr;
}

QByteArray upscaleX11ModeTiming(const QPoint &position, const QSize &size)
{
    xcb_connection_t *connection = kwinApp()->x11Connection();
    if (!connection) {
        return {};
    }
    const auto resources = UniqueCPtr<xcb_randr_get_screen_resources_current_reply_t>(xcb_randr_get_screen_resources_current_reply(connection,
                                                                                                                                   xcb_randr_get_screen_resources_current(connection, kwinApp()->x11RootWindow()), nullptr));
    if (!resources) {
        return {};
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
        if (const auto *mode = outputMode(resources.get(), output.get(), size)) {
            QByteArray encoded;
            QDataStream stream(&encoded, QIODevice::WriteOnly);
            stream.setByteOrder(QDataStream::LittleEndian);
            stream << quint32(mode->id) << quint16(mode->width) << quint16(mode->height) << quint32(mode->dot_clock)
                   << quint16(mode->hsync_start) << quint16(mode->hsync_end) << quint16(mode->htotal) << quint16(mode->hskew)
                   << quint16(mode->vsync_start) << quint16(mode->vsync_end) << quint16(mode->vtotal) << quint16(mode->name_len)
                   << quint32(mode->mode_flags);
            return encoded;
        }
    }
    return {};
}

bool upscaleX11ModeAvailable(const QPoint &position, const QSize &size)
{
    return !upscaleX11ModeTiming(position, size).isEmpty();
}

}
#endif
