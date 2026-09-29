/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The pointer as the test client sees it: where it last moved and pressed, and
// the two ways a game takes it.

#include "x11_client.h"

#include <cstdint>
#include <cstdlib>

QPoint X11Client::lastMotion() const
{
    return m_lastMotion;
}

QPoint X11Client::lastPress() const
{
    return m_lastPress;
}

int X11Client::presses() const
{
    return m_presses;
}

bool X11Client::takePointer()
{
    // A cursor of one transparent pixel: Xwayland reads the window's cursor and
    // treats the window as one that hides it.
    const xcb_pixmap_t pixmap = xcb_generate_id(m_connection);
    xcb_create_pixmap(m_connection, 1, pixmap, m_window, 1, 1);
    const xcb_cursor_t cursor = xcb_generate_id(m_connection);
    xcb_create_cursor(m_connection, cursor, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);
    xcb_free_pixmap(m_connection, pixmap);
    return grabWith(cursor);
}

bool X11Client::confinePointer()
{
    // The arrow of the server's cursor font, set on the window as Wine sets a
    // game's cursor there. A window left with the root's cursor counts as one
    // that hides it under the Xwayland 24.1.10 of Ubuntu 26.04, which then
    // locks the pointer instead of confining it.
    const xcb_font_t font = xcb_generate_id(m_connection);
    xcb_open_font(m_connection, font, 6, "cursor");
    const xcb_cursor_t cursor = xcb_generate_id(m_connection);
    constexpr uint16_t leftPointer = 68;
    xcb_create_glyph_cursor(m_connection, cursor, font, font, leftPointer, leftPointer + 1, 0, 0, 0, 0xffff, 0xffff, 0xffff);
    xcb_close_font(m_connection, font);
    return grabWith(cursor);
}

bool X11Client::grabWith(xcb_cursor_t cursor)
{
    xcb_change_window_attributes(m_connection, m_window, XCB_CW_CURSOR, &cursor);
    const xcb_grab_pointer_cookie_t cookie =
        xcb_grab_pointer(m_connection, 1, m_window,
                         XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, m_window, cursor, XCB_CURRENT_TIME);
    xcb_generic_error_t *error = nullptr;
    xcb_grab_pointer_reply_t *reply = xcb_grab_pointer_reply(m_connection, cookie, &error);
    const bool taken = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
    std::free(reply);
    std::free(error);
    // The window and the grab keep the cursor for as long as they use it.
    xcb_free_cursor(m_connection, cursor);
    xcb_flush(m_connection);
    return taken;
}
