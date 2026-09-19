/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "viewporter-client.h"
#include "xdg-shell-client.h"

#include <QSize>

#include <wayland-client.h>

// A real shared-memory client with an independently sized fullscreen viewport.
// This fixture deliberately avoids Qt's window backing-store size policy.
class WaylandClient
{
public:
    WaylandClient();
    ~WaylandClient();
    bool initialize();
    bool show(const QSize &size, bool opaque = true);
    void commit();
    void fullscreen(bool enabled);
    int descriptor() const;
    void dispatch();
    // Process everything the compositor has already sent. The output's events
    // arrive unprompted, so a test reads them without waiting on a timeout.
    bool roundtrip();

    // What the compositor told this client its screen is, as it arrived on the
    // wire. A game decides what to render from exactly these events, so they
    // are the only direct evidence of what the effect asked it for.
    QSize advertisedMode() const;
    int advertisedScale() const;

private:
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void configure(void *data, xdg_surface *surface, uint32_t serial);
    static void resize(void *data, xdg_toplevel *toplevel, int32_t width, int32_t height, wl_array *states);
    static void outputMode(void *data, wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
    static void outputScale(void *data, wl_output *output, int32_t factor);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_sharedMemory = nullptr;
    wl_output *m_output = nullptr;
    xdg_wm_base *m_shell = nullptr;
    wp_viewporter *m_viewporter = nullptr;
    wl_surface *m_surface = nullptr;
    xdg_surface *m_shellSurface = nullptr;
    xdg_toplevel *m_toplevel = nullptr;
    wp_viewport *m_viewport = nullptr;
    wl_buffer *m_buffer = nullptr;
    QSize m_size;
    QSize m_destination{128, 128};
    QSize m_advertisedMode;
    int m_advertisedScale = 0;
    bool m_opaque = true;
    bool m_configured = false;
};
