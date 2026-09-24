/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QRect>

#include <xcb/xcb.h>

// Each instance owns a connection: RandR emulation belongs to that connection,
// so a second window on the test process's first connection is not isolation.
class X11Client : public QObject
{
public:
    explicit X11Client(bool cooperative = true, int ignoredResizes = 0);
    ~X11Client() override;
    bool show(const QByteArray &identity, const QRect &geometry, bool fullscreen = true);
    bool waitForMapping();
    QRect geometry() const;
    bool isFullscreen() const;
    bool mode(const QSize &size);
    void fullscreen(bool enabled);
    void resize(const QSize &size);
    /** Where the last pointer motion landed, in the window's own coordinates. */
    QPoint lastMotion() const;
    /** Where the last button press landed, in the window's own coordinates. */
    QPoint lastPress() const;
    /** How many button presses the window has received. */
    int presses() const;
    /** Whether the window holds the keyboard focus, from X11's own events. */
    bool isFocused() const;
    /** How many times it lost that focus. */
    int focusLosses() const;
    /**
     * Take the pointer the way a game's mouse look does: hide the cursor and
     * grab the pointer confined to the window, which is what makes Xwayland ask
     * the compositor to lock it (hw/xwayland/xwayland-input.c,
     * xwl_seat_maybe_lock_on_hidden_cursor).
     */
    bool takePointer();
    /**
     * How many ConfigureNotify events the window has received, synthetic
     * ones included. A resize the window manager refuses is still answered
     * with one (ICCCM 4.1.5), so this is how a test waits for the answer to
     * its own resize rather than for a delay it hopes covers it.
     */
    int configureNotifies() const;
    /** Sizes reported by ConfigureNotify, in arrival order. */
    QList<QSize> configuredSizes() const;
    /**
     * How many times the window manager asked the window to close, with
     * WM_DELETE_WINDOW, which the window says it understands. A window that
     * does not say so is killed instead, and its client with it.
     */
    int closeRequests() const;
    /**
     * Report this process as the window's owner, in _NET_WM_PID, when shown.
     *
     * Off unless asked: KWin groups a process's windows by it, and the cases
     * that do not ask are about windows whose program is not known.
     */
    void reportProcess();

private:
    xcb_atom_t atom(const QByteArray &name) const;
    void dispatch();
    void paint(const QSize &size);
    xcb_connection_t *m_connection = nullptr;
    xcb_screen_t *m_screen = nullptr;
    xcb_window_t m_window = XCB_NONE;
    xcb_gcontext_t m_context = XCB_NONE;
    QPoint m_position;
    QSize m_size;
    QPoint m_lastMotion{-1, -1};
    QPoint m_lastPress{-1, -1};
    int m_presses = 0;
    xcb_cursor_t m_blankCursor = XCB_NONE;
    bool m_focused = false;
    int m_focusLosses = 0;
    int m_configureNotifies = 0;
    QList<QSize> m_configuredSizes;
    int m_closeRequests = 0;
    xcb_atom_t m_protocols = XCB_NONE;
    xcb_atom_t m_deleteWindow = XCB_NONE;
    bool m_cooperative;
    int m_ignoredResizes = 0;
    bool m_fullscreenOnMap = false;
    bool m_reportsProcess = false;
};
