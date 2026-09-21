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
    QRect geometry() const;
    bool isFullscreen() const;
    bool mode(const QSize &size);
    void fullscreen(bool enabled);
    void resize(const QSize &size);
    /** Where the last pointer motion landed, in the window's own coordinates. */
    QPoint lastMotion() const;
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
    bool m_cooperative;
    int m_ignoredResizes = 0;
    bool m_fullscreenOnMap = false;
    bool m_reportsProcess = false;
};
