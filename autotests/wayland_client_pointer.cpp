/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The pointer as the test client sees it: where it last moved, in the surface
// coordinates a game maps onto its buffer, and a confinement as a game takes
// one for its mouse look.

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
            if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) || client->m_pointer) {
                return;
            }
            client->m_pointer = wl_seat_get_pointer(seat);
            static const wl_pointer_listener pointerListener = [] {
                wl_pointer_listener events{};
                events.enter = [](void *data, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t x, wl_fixed_t y) {
                    static_cast<WaylandClient *>(data)->m_lastMotion = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                };
                events.leave = [](void *, wl_pointer *, uint32_t, wl_surface *) { };
                events.motion = [](void *data, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y) {
                    static_cast<WaylandClient *>(data)->m_lastMotion = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
                };
                events.button = [](void *, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t) { };
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
