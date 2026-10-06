/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wayland_client.h"

#include <QCoreApplication>
#include <QImage>
#include <QTemporaryFile>
#include <QTest>

#include <algorithm>
#include <cstring>

WaylandClient::WaylandClient(uint32_t outputVersion, bool fractionalScale)
    : m_outputVersion(outputVersion)
    , m_useFractionalScale(fractionalScale)
{
}

WaylandClient::~WaylandClient()
{
    releasePointer();
    releasePopup();
    for (wl_callback *frame : m_frames) {
        wl_callback_destroy(frame);
    }
    if (m_buffer) {
        wl_buffer_destroy(m_buffer);
    }
    if (m_toplevel) {
        xdg_toplevel_destroy(m_toplevel);
        xdg_surface_destroy(m_shellSurface);
        wp_viewport_destroy(m_viewport);
        if (m_fractionalScale) {
            wp_fractional_scale_v1_destroy(m_fractionalScale);
        }
        wl_surface_destroy(m_surface);
    }
    if (m_fractionalScaleManager) {
        wp_fractional_scale_manager_v1_destroy(m_fractionalScaleManager);
    }
    if (m_viewporter) {
        wp_viewporter_destroy(m_viewporter);
    }
    if (m_shell) {
        xdg_wm_base_destroy(m_shell);
    }
    if (m_sharedMemory) {
        wl_shm_destroy(m_sharedMemory);
    }
    for (const auto &output : m_outputs) {
        if (output->logical) {
            zxdg_output_v1_destroy(output->logical);
        }
        wl_output_destroy(output->proxy);
    }
    if (m_outputManager) {
        zxdg_output_manager_v1_destroy(m_outputManager);
    }
    if (m_compositor) {
        wl_compositor_destroy(m_compositor);
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
    if (m_display) {
        wl_display_flush(m_display);
        wl_display_disconnect(m_display);
    }
}

void WaylandClient::global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto client = static_cast<WaylandClient *>(data);
    if (std::strcmp(interface, "wl_compositor") == 0) {
        client->m_compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(interface, "wl_shm") == 0) {
        client->m_sharedMemory = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, "wl_output") == 0) {
        // A game enumerates displays to decide what to render, and that is the
        // moment the effect makes its request. Binding every output here is
        // what lets this client observe what it was told about each.
        auto output = std::make_unique<Output>();
        output->name = name;
        output->proxy = static_cast<wl_output *>(wl_registry_bind(registry, name, &wl_output_interface, client->m_outputVersion));
        static const wl_output_listener listener{
            [](void *, wl_output *, int32_t, int32_t, int32_t, int32_t, int32_t, const char *, const char *, int32_t) {},
            outputMode,
            // Since version 3 of xdg_output, this done applies its logical
            // size too, which is what a program then goes by.
            [](void *data, wl_output *) {
            auto output = static_cast<Output *>(data);
            if (output->logical && output->logicalSize.isValid()) {
                output->appliedLogicalSizes.append(output->logicalSize);
            }
        },
            outputScale,
            nullptr,
            nullptr,
        };
        wl_output_add_listener(output->proxy, &listener, output.get());
        client->m_outputs.push_back(std::move(output));
    } else if (std::strcmp(interface, "wp_fractional_scale_manager_v1") == 0 && client->m_useFractionalScale) {
        client->m_fractionalScaleManager = static_cast<wp_fractional_scale_manager_v1 *>(
            wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
    } else if (std::strcmp(interface, "zxdg_output_manager_v1") == 0) {
        client->m_outputManager = static_cast<zxdg_output_manager_v1 *>(
            wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, std::min(version, 3U)));
    } else if (std::strcmp(interface, "wp_viewporter") == 0) {
        client->m_viewporter = static_cast<wp_viewporter *>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (std::strcmp(interface, "xdg_wm_base") == 0) {
        client->m_shell = static_cast<xdg_wm_base *>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        static const xdg_wm_base_listener listener{[](void *, xdg_wm_base *shell, uint32_t serial) {
            xdg_wm_base_pong(shell, serial);
        }};
        xdg_wm_base_add_listener(client->m_shell, &listener, nullptr);
    } else if (std::strcmp(interface, "wl_seat") == 0 && !client->m_seat) {
        client->bindSeat(registry, name);
    } else if (std::strcmp(interface, "zwp_pointer_constraints_v1") == 0) {
        client->m_constraints = static_cast<zwp_pointer_constraints_v1 *>(
            wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
    } else if (std::strcmp(interface, "zwp_relative_pointer_manager_v1") == 0) {
        client->m_relativeManager = static_cast<zwp_relative_pointer_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
    }
}

bool WaylandClient::initialize(bool fullscreen)
{
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    static const wl_registry_listener listener{global, globalRemoved};
    wl_registry_add_listener(m_registry, &listener, this);
    if (wl_display_roundtrip(m_display) < 0 || !m_compositor || !m_sharedMemory || !m_shell || !m_viewporter) {
        return false;
    }
    // Every output's xdg_output, once all are bound, in the order they were
    // announced, as SDL asks for them.
    for (const auto &output : m_outputs) {
        if (!m_outputManager) {
            break;
        }
        output->logical = zxdg_output_manager_v1_get_xdg_output(m_outputManager, output->proxy);
        static const zxdg_output_v1_listener listener{
            [](void *, zxdg_output_v1 *, int32_t, int32_t) {},
            [](void *data, zxdg_output_v1 *, int32_t width, int32_t height) {
            static_cast<Output *>(data)->logicalSize = QSize(width, height);
        },
            [](void *, zxdg_output_v1 *) {},
            [](void *, zxdg_output_v1 *, const char *) {},
            [](void *, zxdg_output_v1 *, const char *) {},
        };
        zxdg_output_v1_add_listener(output->logical, &listener, output.get());
    }
    // A second round trip: the output's own events follow the bind, and the
    // request the effect makes is one of them.
    if (wl_display_roundtrip(m_display) < 0) {
        return false;
    }
    m_surface = wl_compositor_create_surface(m_compositor);
    m_viewport = wp_viewporter_get_viewport(m_viewporter, m_surface);
    // Bound and listened to, never honoured on its own: what the client draws
    // is what each case shows, so a case decides whether it follows the scale.
    if (m_fractionalScaleManager) {
        m_fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(m_fractionalScaleManager, m_surface);
        static const wp_fractional_scale_v1_listener fractionalListener{[](void *data, wp_fractional_scale_v1 *, uint32_t scale) {
            static_cast<WaylandClient *>(data)->m_preferredScale = int(scale);
        }};
        wp_fractional_scale_v1_add_listener(m_fractionalScale, &fractionalListener, this);
    }
    m_shellSurface = xdg_wm_base_get_xdg_surface(m_shell, m_surface);
    static const xdg_surface_listener surfaceListener{configure};
    xdg_surface_add_listener(m_shellSurface, &surfaceListener, this);
    m_toplevel = xdg_surface_get_toplevel(m_shellSurface);
    static const xdg_toplevel_listener toplevelListener{resize, [](void *, xdg_toplevel *) { }, nullptr, nullptr};
    xdg_toplevel_add_listener(m_toplevel, &toplevelListener, this);
    xdg_toplevel_set_app_id(m_toplevel, "org.kde.upscale.integrationtest");
    xdg_toplevel_set_title(m_toplevel, "Upscale integration test");
    if (fullscreen) {
        xdg_toplevel_set_fullscreen(m_toplevel, nullptr);
    }
    wl_surface_commit(m_surface);
    return wl_display_roundtrip(m_display) >= 0;
}

void WaylandClient::outputMode(void *data, wl_output *, uint32_t flags, int32_t width, int32_t height, int32_t)
{
    // Only the mode the screen is said to be in now. A client picking a size
    // reads that one, and the effect replaces exactly it.
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        static_cast<Output *>(data)->mode = QSize(width, height);
    }
}

void WaylandClient::outputScale(void *data, wl_output *, int32_t factor)
{
    static_cast<Output *>(data)->scale = factor;
}

void WaylandClient::globalRemoved(void *data, wl_registry *, uint32_t name)
{
    for (const auto &output : static_cast<WaylandClient *>(data)->m_outputs) {
        if (output->name == name) {
            output->offered = false;
        }
    }
}

QSize WaylandClient::advertisedMode(int output) const
{
    return output >= 0 && output < int(m_outputs.size()) ? m_outputs.at(output)->mode : QSize();
}

bool WaylandClient::offered(int output) const
{
    return output >= 0 && output < int(m_outputs.size()) && m_outputs.at(output)->offered;
}

int WaylandClient::preferredScale() const
{
    return m_preferredScale;
}

int WaylandClient::presentedFrames() const
{
    return m_presentedFrames;
}

bool WaylandClient::presentFrames(int count)
{
    for (int frame = 0; frame < count; ++frame) {
        const int presented = m_presentedFrames;
        commit();
        if (!QTest::qWaitFor([this, presented]() {
            return m_presentedFrames > presented;
        }, 5000)) {
            return false;
        }
    }
    return true;
}

void WaylandClient::framePresented(void *data, wl_callback *callback, uint32_t time)
{
    Q_UNUSED(time)
    auto client = static_cast<WaylandClient *>(data);
    std::erase(client->m_frames, callback);
    wl_callback_destroy(callback);
    ++client->m_presentedFrames;
}

int WaylandClient::advertisedScale(int output) const
{
    return output >= 0 && output < int(m_outputs.size()) ? m_outputs.at(output)->scale : 0;
}

QList<QSize> WaylandClient::appliedLogicalSizes(int output) const
{
    return output >= 0 && output < int(m_outputs.size()) ? m_outputs.at(output)->appliedLogicalSizes : QList<QSize>();
}

QSize WaylandClient::advertisedLogicalSize(int output) const
{
    return output >= 0 && output < int(m_outputs.size()) ? m_outputs.at(output)->logicalSize : QSize();
}

void WaylandClient::configure(void *data, xdg_surface *surface, uint32_t serial)
{
    auto client = static_cast<WaylandClient *>(data);
    xdg_surface_ack_configure(surface, serial);
    client->m_configured = true;
    client->commit();
}

void WaylandClient::resize(void *data, xdg_toplevel *, int32_t width, int32_t height, wl_array *)
{
    auto client = static_cast<WaylandClient *>(data);
    if (width > 0 && height > 0) {
        client->m_destination = QSize(width, height);
    }
}

bool WaylandClient::show(const QSize &size, bool opaque)
{
    // Process pending fullscreen configure events before committing a buffer.
    if (wl_display_roundtrip(m_display) < 0) {
        return false;
    }
    if (m_buffer) {
        wl_buffer_destroy(m_buffer);
    }
    m_size = size;
    m_opaque = opaque;
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(opaque ? QColor(Qt::red) : QColor(128, 0, 0, 128));
    // Each commit owns a separate backing file. A roundtrip does not promise
    // that the compositor has finished reading the previous buffer.
    QTemporaryFile storage(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/buffer-XXXXXX"));
    if (!storage.open()) {
        return false;
    }
    if (storage.write(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()) != image.sizeInBytes()
        || !storage.flush()) {
        return false;
    }
    wl_shm_pool *pool = wl_shm_create_pool(m_sharedMemory, storage.handle(), int(image.sizeInBytes()));
    m_buffer = wl_shm_pool_create_buffer(pool, 0, size.width(), size.height(), int(image.bytesPerLine()),
                                         opaque ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    commit();
    return true;
}

void WaylandClient::commit()
{
    if (!m_configured || !m_buffer) {
        return;
    }
    wp_viewport_set_destination(m_viewport, m_destination.width(), m_destination.height());
    wl_region *region = wl_compositor_create_region(m_compositor);
    if (m_opaque) {
        wl_region_add(region, 0, 0, m_destination.width(), m_destination.height());
    }
    wl_surface_set_opaque_region(m_surface, region);
    wl_region_destroy(region);
    wl_surface_attach(m_surface, m_buffer, 0, 0);
    wl_surface_damage_buffer(m_surface, 0, 0, m_size.width(), m_size.height());
    static const wl_callback_listener frameListener{framePresented};
    m_frames.push_back(wl_surface_frame(m_surface));
    wl_callback_add_listener(m_frames.back(), &frameListener, this);
    wl_surface_commit(m_surface);
    wl_display_flush(m_display);
}

void WaylandClient::fullscreen(bool enabled)
{
    if (enabled) {
        xdg_toplevel_set_fullscreen(m_toplevel, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(m_toplevel);
    }
    wl_display_flush(m_display);
}

void WaylandClient::resize(const QSize &destination)
{
    m_destination = destination;
    commit();
}

int WaylandClient::descriptor() const
{
    return wl_display_get_fd(m_display);
}

bool WaylandClient::roundtrip()
{
    return wl_display_roundtrip(m_display) >= 0;
}

void WaylandClient::dispatch()
{
    if (wl_display_dispatch(m_display) < 0) {
        QCoreApplication::exit(1);
    }
    wl_display_flush(m_display);
}
