/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11input.h"

#if KWIN_BUILD_X11
#include "x11resolution.h"

#include "input_event.h"
#include "pointer_input.h"
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"

namespace KWin
{

UpscaleX11Input::UpscaleX11Input(const UpscaleX11Resolution *control)
    // Ahead of KWin's forwarding filter, which is what reads the seat's
    // focus and transformation, and behind everything that may take the
    // event away from the window first. The Xwayland slot is where KWin
    // keeps its own handling of these clients.
    : InputEventFilter(InputFilterOrder::XWayland)
    , m_control(control)
{
    if (input()) {
        input()->installInputEventFilter(this);
    }
}

UpscaleX11Input::~UpscaleX11Input()
{
    m_control = nullptr;
    refresh();
}

// Undoes what this filter did to the seat. Where KWin's own hit test focuses
// the window, KWin's transformation goes back; where only this filter had
// focused it, the focus is withdrawn.
void UpscaleX11Input::withdraw(SeatInterface *seat)
{
    if (m_surface && seat->focusedPointerSurface() == m_surface) {
        Window *window = input()->pointer()->focus();
        if (window && window->surface() == m_surface) {
            seat->setFocusedPointerSurfaceTransformation(window->inputTransformation());
        } else {
            seat->notifyPointerLeave();
        }
    }
    m_surface = nullptr;
}

QPointF UpscaleX11Input::apply(const QPointF &position)
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat) {
        return QPointF(1, 1);
    }
    SurfaceInterface *focused = seat->focusedPointerSurface();
    SurfaceInterface *surface = nullptr;
    QPointF origin;
    const QPointF scale = m_control ? m_control->presentedUnder(position, &surface, &origin) : QPointF(1, 1);
    if (!surface || (focused && focused != surface)) {
        // Nothing presented here, or KWin found a window of its own on top of
        // what is: a dialog, an overlay. Either way the seat is KWin's.
        withdraw(seat);
        return QPointF(1, 1);
    }
    // Translate first, then scale: a point on the output becomes an offset
    // from the window's origin, and that offset is shrunk by the ratio of the
    // surface to the frame. QMatrix4x4 applies the operation added last
    // first, so the order here is the reverse of the order applied.
    QMatrix4x4 transformation;
    transformation.scale(float(scale.x()), float(scale.y()));
    transformation.translate(float(-origin.x()), float(-origin.y()));
    if (!focused) {
        // KWin's hit test ended in the surface's own input region and found
        // nothing; the frame is there all the same.
        seat->notifyPointerEnter(surface, position, transformation);
    } else if (seat->focusedPointerSurfaceTransformation() != transformation) {
        // Set by KWin when the surface gained focus or its window moved, or
        // stale from an earlier size of the surface.
        seat->setFocusedPointerSurfaceTransformation(transformation);
    }
    m_surface = surface;
    return scale;
}

void UpscaleX11Input::refresh()
{
    if (input()) {
        apply(input()->pointer()->pos());
    }
}

bool UpscaleX11Input::pointerMotion(PointerMotionEvent *event)
{
    const QPointF scale = apply(event->position);
    // Relative motion is defined in the surface's coordinates too, so a
    // surface presented larger than it is receives smaller deltas. Xwayland's
    // emulation scales them by the same factor (xwayland-input.c, relative
    // and unaccelerated alike), and a game's mouse look reads exactly these.
    event->delta = QPointF(event->delta.x() * scale.x(), event->delta.y() * scale.y());
    event->deltaUnaccelerated = QPointF(event->deltaUnaccelerated.x() * scale.x(),
                                        event->deltaUnaccelerated.y() * scale.y());
    return false;
}

bool UpscaleX11Input::pointerButton(PointerButtonEvent *event)
{
    apply(event->position);
    return false;
}

bool UpscaleX11Input::pointerAxis(PointerAxisEvent *event)
{
    apply(event->position);
    return false;
}

} // namespace KWin
#endif
