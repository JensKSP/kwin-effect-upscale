/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "config-kwin.h"

#if KWIN_BUILD_X11
#include "input.h"

#include <QMatrix4x4>
#include <QPointer>
#include <QRectF>

#include <functional>

namespace KWin
{
class SeatInterface;
class SurfaceInterface;
class UpscaleX11Resolution;
struct UpscalePresentedPointer;
class Window;

/**
 * Maps pointer input to a resized X11 window and repairs incomplete coverage.
 *
 * KWin keeps such a window's frame at the output while its X window, and so
 * its surface, is the smaller size the effect asked for. Two things follow.
 * Over the part of the output the surface still occupies, KWin focuses it and
 * hands it the pointer position translated by the window's origin, which
 * Xwayland forwards unchanged to an X window a fraction of that size. Over
 * the rest, KWin's hit test goes through the surface's own input region and
 * finds nothing at all, so the pointer reaches no window.
 *
 * Xwayland's own mode emulation solves the first by scaling the coordinates
 * it receives by surface size over output size. Its viewport enlarges the
 * surface, but an explicit input shape can remain at the drawable's size.
 * For that complete rectangular shape this filter repairs focus and click
 * ownership with a unit transform; Xwayland keeps coordinate scaling.
 * Empty, inset and nonrectangular shapes remain the client's choice.
 * For effect-presented windows this filter does
 * the same one step earlier: it gives the seat a transformation that scales as
 * well as translates, and over the rest of the frame it focuses that surface on
 * the seat itself, whatever KWin's hit test found beside the surface - the
 * desktop, a panel, nothing at all - and restores KWin's focus when the pointer leaves
 * the frame. A window stacked above the presented one, a dialog or an on-screen
 * display, is left to KWin.
 *
 * Where the pointer is this filter's rather than KWin's, the events for it are
 * delivered here as well, because the filters in between would otherwise act on
 * the window KWin found: a click would raise and activate what lies under the
 * game instead of reaching the game. Motion is left to KWin's forwarding, which
 * sends it to the surface on the seat, the presented one.
 */
class UpscaleX11Input : public InputEventFilter
{
public:
    explicit UpscaleX11Input(const UpscaleX11Resolution *control);
    ~UpscaleX11Input() override;

    bool pointerMotion(PointerMotionEvent *event) override;
    bool pointerButton(PointerButtonEvent *event) override;
    bool pointerAxis(PointerAxisEvent *event) override;

    /** Reconsider the pointer where it is, rather than at its next event. */
    void refresh();

private:
    // Brings the seat's focus and transformation in line with what is
    // presented under @p position, and answers the scale now in force, which
    // is one where nothing was applied.
    QPointF apply(const QPointF &position);
    void withdraw(SeatInterface *seat);
    void recordMapping(const UpscalePresentedPointer &presented, const QPointF &position, const QMatrix4x4 &transformation);
    // Sends what KWin's forwarding would have sent, and answers whether it did,
    // which is whether the event is not KWin's to handle any more.
    bool deliver(const std::function<void(SeatInterface *seat)> &send);

    const UpscaleX11Resolution *m_control;
    // The surface whose seat state this filter changed. KWin refreshes its
    // own only on focus and geometry changes, and neither happens when a
    // request ends, so what goes back is decided at that moment from KWin's
    // own focus: its transformation where KWin focuses the window, the
    // underlying surface where it finds one, and a leave where it finds none.
    QPointer<SurfaceInterface> m_surface;
    QMatrix4x4 m_transformation;
    QRectF m_client;
    // The presented window whose pointer this is, while it is not the window
    // KWin found under it.
    QPointer<Window> m_claimed;
};

} // namespace KWin
#endif
