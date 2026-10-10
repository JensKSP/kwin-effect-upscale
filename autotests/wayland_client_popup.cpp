/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A popup of the test client's, as a game's menu opens one: a surface of its
// own beside the game's, placed in the game's surface coordinates.

#include "wayland_client.h"

#include <QImage>
#include <QTemporaryFile>

bool WaylandClient::openPopup(const QRect &place)
{
    if (!m_shell || !m_shellSurface || m_popup || place.isEmpty()) {
        return false;
    }
    m_popupSurface = wl_compositor_create_surface(m_compositor);
    m_popupShellSurface = xdg_wm_base_get_xdg_surface(m_shell, m_popupSurface);
    static const xdg_surface_listener surfaceListener{[](void *data, xdg_surface *surface, uint32_t serial) {
        xdg_surface_ack_configure(surface, serial);
        static_cast<WaylandClient *>(data)->m_popupConfigured = true;
    }};
    xdg_surface_add_listener(m_popupShellSurface, &surfaceListener, this);
    xdg_positioner *positioner = xdg_wm_base_create_positioner(m_shell);
    xdg_positioner_set_size(positioner, place.width(), place.height());
    xdg_positioner_set_anchor_rect(positioner, place.x(), place.y(), 1, 1);
    xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
    xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    m_popup = xdg_surface_get_popup(m_popupShellSurface, m_shellSurface, positioner);
    xdg_positioner_destroy(positioner);
    // Assigned by name, as the seat's: the listener grows with the headers.
    static const xdg_popup_listener popupListener = [] {
        xdg_popup_listener events{};
        events.configure = [](void *, xdg_popup *, int32_t, int32_t, int32_t, int32_t) { };
        events.popup_done = [](void *, xdg_popup *) { };
        return events;
    }();
    xdg_popup_add_listener(m_popup, &popupListener, this);
    wl_surface_commit(m_popupSurface);
    if (wl_display_roundtrip(m_display) < 0 || !m_popupConfigured) {
        return false;
    }
    QImage image(place.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    QTemporaryFile storage(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/popup-XXXXXX"));
    if (!storage.open() || storage.write(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()) != image.sizeInBytes()
        || !storage.flush()) {
        return false;
    }
    wl_shm_pool *pool = wl_shm_create_pool(m_sharedMemory, storage.handle(), int(image.sizeInBytes()));
    m_popupBuffer = wl_shm_pool_create_buffer(pool, 0, place.width(), place.height(), int(image.bytesPerLine()), WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    wl_surface_attach(m_popupSurface, m_popupBuffer, 0, 0);
    wl_surface_damage_buffer(m_popupSurface, 0, 0, place.width(), place.height());
    wl_surface_commit(m_popupSurface);
    return wl_display_roundtrip(m_display) >= 0;
}

QPointF WaylandClient::popupMotion() const
{
    return m_popupMotion;
}

void WaylandClient::releasePopup()
{
    if (m_popup) {
        xdg_popup_destroy(m_popup);
    }
    if (m_popupShellSurface) {
        xdg_surface_destroy(m_popupShellSurface);
    }
    if (m_popupBuffer) {
        wl_buffer_destroy(m_popupBuffer);
    }
    if (m_popupSurface) {
        wl_surface_destroy(m_popupSurface);
    }
}
