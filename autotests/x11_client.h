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
    /**
     * Receives everything the X server sent this client before now: a round
     * trip, then the events it overtook. A configure the window manager sent
     * before the caller last heard from it has then arrived.
     */
    void sync();
    QRect geometry() const;
    bool isFullscreen() const;
    bool mode(const QSize &size);
    void inputShape(const QRect &rectangle);
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
     * Confine the pointer to the window with a cursor of its own shown, as
     * Wine does for a fullscreen game, which Xwayland passes on as a
     * confinement of the window's surface.
     */
    bool confinePointer();
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
     * The size the server gives the window as the client learns it was first
     * mapped, or an invalid size before that. Asked of the server rather than
     * taken from the events: a mapping the effect held is released under a
     * server grab, and the question waits for the grab to end, so it reads what
     * the client will draw at, the size its first frame is made for.
     */
    QSize sizeAtMapping() const;
    /**
     * How many times the window manager asked the window to close, with
     * WM_DELETE_WINDOW, which the window says it understands. A window that
     * does not say so is killed instead, and its client with it.
     */
    int closeRequests() const;
    /**
     * Withdraw the window the way XWithdrawWindow does: unmap it, and tell the
     * window manager with the synthetic UnmapNotify ICCCM 4.1.4 sends to the
     * root, which is all it hears of a window it has not mapped yet.
     */
    void withdraw();
    /** How many UnmapNotify events the window has received. */
    int unmapNotifies() const;
    /** Whether the window is mapped and all its ancestors are. */
    bool isViewable() const;
    /**
     * Report this process as the window's owner, in _NET_WM_PID, when shown.
     *
     * Off unless asked: KWin groups a process's windows by it, and the cases
     * that do not ask are about windows whose program is not known.
     */
    void reportProcess(qint64 pid = 0);
    /**
     * Leave the window to the window manager's decoration, when shown, rather
     * than asking for none as a borderless game does.
     */
    void keepDecoration();

private:
    xcb_atom_t atom(const QByteArray &name) const;
    void dispatch();
    void paint(const QSize &size);
    /** Show @p cursor on the window and grab the pointer confined to it, with it. */
    bool grabWith(xcb_cursor_t cursor);
    xcb_connection_t *m_connection = nullptr;
    xcb_screen_t *m_screen = nullptr;
    xcb_window_t m_window = XCB_NONE;
    xcb_gcontext_t m_context = XCB_NONE;
    QPoint m_position;
    QSize m_size;
    QPoint m_lastMotion{-1, -1};
    QPoint m_lastPress{-1, -1};
    int m_presses = 0;
    bool m_focused = false;
    int m_focusLosses = 0;
    int m_configureNotifies = 0;
    QList<QSize> m_configuredSizes;
    QSize m_sizeAtMapping;
    int m_closeRequests = 0;
    int m_unmapNotifies = 0;
    xcb_atom_t m_protocols = XCB_NONE;
    xcb_atom_t m_deleteWindow = XCB_NONE;
    bool m_cooperative;
    int m_ignoredResizes = 0;
    bool m_fullscreenOnMap = false;
    qint64 m_reportedProcess = 0;
    bool m_decorated = false;
};
