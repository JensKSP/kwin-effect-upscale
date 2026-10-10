/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The pointer as the test client sees it: where it last moved, in the surface
// coordinates a game maps onto its buffer, how often a button went down on it,
// and a confinement, a lock and relative motion as a game's mouse look takes
// and reads them.

#include "wayland_client.h"

void WaylandClient::bindSeat(wl_registry *registry, uint32_t name)
{
    // The first version: motion arrives on its own, with no frame event to
    // wait for, and no event this client does not answer is ever sent.
    m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    // Assigned by name, because the listener grows with each release of the
    // headers and only the events of the version bound can arrive.
    static const wl_seat_listener seatListener = [] {
        wl_seat_listener events{};
        events.capabilities = [](void *data, wl_seat *seat, uint32_t capabilities) {
            auto client = static_cast<WaylandClient *>(data);
            if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !client->m_touch) {
                client->m_touch = wl_seat_get_touch(seat);
                static const wl_touch_listener touchListener = [] {
                    wl_touch_listener events{};
                    events.down = [](void *data, wl_touch *, uint32_t, uint32_t, wl_surface *, int32_t, wl_fixed_t x, wl_fixed_t y) {
                        auto client = static_cast<WaylandClient *>(data);
                        client->m_lastTouch = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                        ++client->m_touchesDown;
                    };
                    events.up = [](void *, wl_touch *, uint32_t, uint32_t, int32_t) { };
                    events.motion = [](void *data, wl_touch *, uint32_t, int32_t, wl_fixed_t x, wl_fixed_t y) {
                        static_cast<WaylandClient *>(data)->m_lastTouch = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                    };
                    events.frame = [](void *, wl_touch *) { };
                    events.cancel = [](void *, wl_touch *) { };
                    return events;
                }();
                wl_touch_add_listener(client->m_touch, &touchListener, client);
            }
            if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) || client->m_pointer) {
                return;
            }
            client->m_pointer = wl_seat_get_pointer(seat);
            static const wl_pointer_listener pointerListener = [] {
                wl_pointer_listener events{};
                events.enter = [](void *data, wl_pointer *, uint32_t, wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
                    auto client = static_cast<WaylandClient *>(data);
                    client->m_pointerOn = surface;
                    (surface == client->m_popupSurface ? client->m_popupMotion : client->m_lastMotion) = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                };
                events.leave = [](void *data, wl_pointer *, uint32_t, wl_surface *) {
                    static_cast<WaylandClient *>(data)->m_pointerOn = nullptr;
                };
                events.motion = [](void *data, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                    auto client = static_cast<WaylandClient *>(data);
                    const bool popup = client->m_popupSurface && client->m_pointerOn == client->m_popupSurface;
                    (popup ? client->m_popupMotion : client->m_lastMotion) = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                };
                events.button = [](void *data, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t state) {
                    static_cast<WaylandClient *>(data)->m_presses += state == WL_POINTER_BUTTON_STATE_PRESSED ? 1 : 0;
                };
                events.axis = [](void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) { };
                return events;
            }();
            wl_pointer_add_listener(client->m_pointer, &pointerListener, client);
        };
        return events;
    }();
    wl_seat_add_listener(m_seat, &seatListener, this);
}

void WaylandClient::releasePointer()
{
    if (m_touch) {
        wl_touch_destroy(m_touch);
    }
    if (m_relativePointer) {
        zwp_relative_pointer_v1_destroy(m_relativePointer);
    }
    if (m_relativeManager) {
        zwp_relative_pointer_manager_v1_destroy(m_relativeManager);
    }
    if (m_lock) {
        zwp_locked_pointer_v1_destroy(m_lock);
    }
    if (m_confinement) {
        zwp_confined_pointer_v1_destroy(m_confinement);
    }
    if (m_constraints) {
        zwp_pointer_constraints_v1_destroy(m_constraints);
    }
    if (m_pointer) {
        wl_pointer_destroy(m_pointer);
    }
    if (m_seat) {
        wl_seat_destroy(m_seat);
    }
}

QPointF WaylandClient::lastMotion() const
{
    return m_lastMotion;
}

int WaylandClient::presses() const
{
    return m_presses;
}

bool WaylandClient::confinePointer()
{
    if (!m_constraints || !m_pointer || !m_surface || m_confinement) {
        return false;
    }
    // The whole surface, for as long as the client holds it.
    m_confinement = zwp_pointer_constraints_v1_confine_pointer(m_constraints, m_surface, m_pointer, nullptr,
                                                               ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    static const zwp_confined_pointer_v1_listener listener{
        [](void *data, zwp_confined_pointer_v1 *) {
        static_cast<WaylandClient *>(data)->m_confined = true;
    },
        [](void *data, zwp_confined_pointer_v1 *) {
        static_cast<WaylandClient *>(data)->m_confined = false;
    },
    };
    zwp_confined_pointer_v1_add_listener(m_confinement, &listener, this);
    commit();
    return true;
}

bool WaylandClient::pointerConfined() const
{
    return m_confined;
}

bool WaylandClient::lockPointer()
{
    if (!m_constraints || !m_pointer || !m_surface || m_lock) {
        return false;
    }
    m_lock = zwp_pointer_constraints_v1_lock_pointer(m_constraints, m_surface, m_pointer, nullptr,
                                                     ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    static const zwp_locked_pointer_v1_listener listener{
        [](void *data, zwp_locked_pointer_v1 *) {
        static_cast<WaylandClient *>(data)->m_locked = true;
    },
        [](void *data, zwp_locked_pointer_v1 *) {
        static_cast<WaylandClient *>(data)->m_locked = false;
    },
    };
    zwp_locked_pointer_v1_add_listener(m_lock, &listener, this);
    commit();
    return true;
}

bool WaylandClient::pointerLocked() const
{
    return m_locked;
}

bool WaylandClient::watchRelativeMotion()
{
    if (!m_relativeManager || !m_pointer) {
        return false;
    }
    if (!m_relativePointer) {
        m_relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(m_relativeManager, m_pointer);
        static const zwp_relative_pointer_v1_listener listener{
            [](void *data, zwp_relative_pointer_v1 *, uint32_t, uint32_t, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t, wl_fixed_t) {
            static_cast<WaylandClient *>(data)->m_relativeMotion += QPointF(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
        },
        };
        zwp_relative_pointer_v1_add_listener(m_relativePointer, &listener, this);
        wl_display_roundtrip(m_display);
    }
    return true;
}

QPointF WaylandClient::relativeMotion() const
{
    return m_relativeMotion;
}

void WaylandClient::resetRelativeMotion()
{
    m_relativeMotion = QPointF();
}

bool WaylandClient::hasTouch() const
{
    return m_touch;
}

QPointF WaylandClient::lastTouch() const
{
    return m_lastTouch;
}

int WaylandClient::touchesDown() const
{
    return m_touchesDown;
}
