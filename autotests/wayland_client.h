/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "fractional-scale-v1-client.h"
#include "viewporter-client.h"
#include "xdg-output-unstable-v1-client.h"
#include "xdg-shell-client.h"

#include <QList>
#include <QSize>

#include <wayland-client.h>

#include <memory>
#include <vector>

// A real shared-memory client with an independently sized fullscreen viewport.
// This fixture deliberately avoids Qt's window backing-store size policy.
class WaylandClient
{
public:
    // The wl_output version this client binds. A program binds the version its
    // toolkit was written against, and the first one has no scale event.
    explicit WaylandClient(uint32_t outputVersion = 2);
    ~WaylandClient();
    bool initialize(bool fullscreen = true);
    bool show(const QSize &size, bool opaque = true);
    void commit();
    void fullscreen(bool enabled);
    void resize(const QSize &destination);
    int descriptor() const;
    void dispatch();
    // Process everything the compositor has already sent. The output's events
    // arrive unprompted, so a test reads them without waiting on a timeout.
    bool roundtrip();

    // What the compositor told this client each screen is, as it arrived on
    // the wire, counting outputs in the order they were announced. A game
    // decides what to render from exactly these events, so they are the only
    // direct evidence of what the effect asked it for.
    QSize advertisedMode(int output = 0) const;
    int advertisedScale(int output = 0) const;
    /** The logical size that output's xdg_output reported last. */
    QSize advertisedLogicalSize(int output = 0) const;
    /** The logical size in force at each of that output's done events, in order. */
    QList<QSize> appliedLogicalSizes(int output = 0) const;
    // Whether the compositor still offers that output. One that goes away is
    // withdrawn from the registry, and the client keeps what it bound.
    bool offered(int output) const;
    /** The last preferred scale the compositor sent, in 120ths, or zero before any. */
    int preferredScale() const;
    /**
     * How many of this client's commits the compositor has presented, counted
     * by their frame callbacks. A test that runs a client for a number of
     * frames waits for this, so that a frame is one KWin drew rather than a
     * stretch of time that may or may not have held one.
     */
    int presentedFrames() const;
    /**
     * Commits @p count frames, each once the compositor has presented the one
     * before, which the caller's event loop hears of. False when one of them
     * was not presented within five seconds.
     */
    bool presentFrames(int count);

private:
    // One bound output. The listener is handed this record, so it keeps its
    // address for as long as the client lives.
    struct Output
    {
        wl_output *proxy = nullptr;
        zxdg_output_v1 *logical = nullptr;
        uint32_t name = 0;
        QSize mode;
        int scale = 0;
        QSize logicalSize;
        QList<QSize> appliedLogicalSizes;
        bool offered = true;
    };

    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    static void globalRemoved(void *data, wl_registry *registry, uint32_t name);
    static void configure(void *data, xdg_surface *surface, uint32_t serial);
    static void resize(void *data, xdg_toplevel *toplevel, int32_t width, int32_t height, wl_array *states);
    static void outputMode(void *data, wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
    static void outputScale(void *data, wl_output *output, int32_t factor);
    static void framePresented(void *data, wl_callback *callback, uint32_t time);

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_compositor *m_compositor = nullptr;
    wl_shm *m_sharedMemory = nullptr;
    uint32_t m_outputVersion;
    std::vector<std::unique_ptr<Output>> m_outputs;
    xdg_wm_base *m_shell = nullptr;
    wp_viewporter *m_viewporter = nullptr;
    wp_fractional_scale_manager_v1 *m_fractionalScaleManager = nullptr;
    wp_fractional_scale_v1 *m_fractionalScale = nullptr;
    zxdg_output_manager_v1 *m_outputManager = nullptr;
    // Frame callbacks not yet answered, destroyed with the client.
    std::vector<wl_callback *> m_frames;
    int m_presentedFrames = 0;
    int m_preferredScale = 0;
    wl_surface *m_surface = nullptr;
    xdg_surface *m_shellSurface = nullptr;
    xdg_toplevel *m_toplevel = nullptr;
    wp_viewport *m_viewport = nullptr;
    wl_buffer *m_buffer = nullptr;
    QSize m_size;
    QSize m_destination{128, 128};
    bool m_opaque = true;
    bool m_configured = false;
};
