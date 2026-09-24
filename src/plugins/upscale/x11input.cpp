/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11input.h"

#if KWIN_BUILD_X11
#include "x11resolution.h"

#include "effect/effecthandler.h"
#include "input_event.h"
#include "pointer_input.h"
#include "wayland/pointer.h"
#include "wayland/pointerconstraints_v1.h"
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

UpscaleX11Input::UpscaleX11Input(const UpscaleX11Resolution *control)
    // Ahead of the filter that acts on a click by raising and activating the
    // window KWin found under the pointer, which over a presented window is
    // the one underneath it, and behind everything that may take the event
    // away from the window first: a popup, a decoration, an interactive move.
    : InputEventFilter(InputFilterOrder::Order(InputFilterOrder::WindowAction - 1))
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
// focused it, restore the surface KWin currently finds, or withdraw focus.
void UpscaleX11Input::withdraw(SeatInterface *seat)
{
    if (m_surface) {
        qCDebug(KWIN_UPSCALE) << "Pointer mapping withdrawn: surface" << m_surface;
    }
    if (m_surface && seat->focusedPointerSurface() == m_surface) {
        Window *window = input()->pointer()->focus();
        if (window && window->surface() == m_surface) {
            seat->setFocusedPointerSurfaceTransformation(window->inputTransformation());
        } else if (window && window->surface()) {
            seat->notifyPointerEnter(window->surface(), input()->pointer()->pos(), window->inputTransformation());
        } else {
            seat->notifyPointerLeave();
        }
    }
    m_surface = nullptr;
}

// Whether KWin found a window of its own above the presented one: a dialog, an
// on-screen display, a panel. Below it is the ordinary case, because a window
// presented across its output covers what KWin's hit test still finds beside
// the smaller surface, the desktop among it.
static bool isAbove(Window *window, Window *presented)
{
    if (!window || window == presented || !effects) {
        return false;
    }
    const QList<EffectWindow *> stacking = effects->stackingOrder();
    return stacking.indexOf(window->effectWindow()) > stacking.indexOf(presented->effectWindow());
}

// A game that locks the pointer, as mouse look does, is given that lock by KWin
// only while KWin's own focus is on its surface, and that focus follows where the
// cursor is in the client's own rectangle rather than in the picture the user
// sees. So the cursor is put inside that rectangle once, which is what makes KWin
// take the lock; a locked pointer is neither shown nor moved afterwards, so
// nothing of this reaches the user. Only where KWin would take the lock at all:
// over the window it would ask about, and while that window is the active one.
static void engageLock(const UpscalePresentedPointer &presented, const QPointF &position)
{
    LockedPointerV1Interface *lock = presented.surface->lockedPointer();
    if (!lock || lock->isLocked() || presented.client.contains(position) || presented.client.isEmpty()) {
        return;
    }
    if (!effects || effects->activeWindow() != presented.window->effectWindow()) {
        return;
    }
    qCInfo(KWIN_UPSCALE) << "Pointer moved to engage requested lock: window" << presented.window->internalId()
                         << "from" << position << "to" << presented.client.center();
    input()->pointer()->warp(presented.client.center());
}

static void confirmEmulatedPosition(SeatInterface *seat, const UpscalePresentedPointer &presented,
                                    const QPointF &position, const QMatrix4x4 &transformation)
{
    if (presented.scale == QPointF(1, 1) && seat->pointer() && seat->pointerPos() == position) {
        // Xwayland 24.1.6 scales motion but not its initial enter. KWin
        // suppresses motion at the position just entered, even when KWin
        // itself set that focus. Deliver the real position before a click
        // can follow; Xwayland maps it exactly once. Respect pointer lock.
        LockedPointerV1Interface *lock = presented.surface->lockedPointer();
        if (!lock || !lock->isLocked()) {
            seat->pointer()->sendMotion(transformation.map(position));
        }
    }
}

QPointF UpscaleX11Input::apply(const QPointF &position)
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat) {
        return QPointF(1, 1);
    }
    SurfaceInterface *focused = seat->focusedPointerSurface();
    const UpscalePresentedPointer presented = m_control ? m_control->presentedUnder(position) : UpscalePresentedPointer{};
    if (presented.isEmpty() || isAbove(input()->pointer()->focus(), presented.window)) {
        // Nothing presented here, or something of KWin's own is on top of what
        // is. Either way the seat is KWin's.
        withdraw(seat);
        m_claimed = nullptr;
        return QPointF(1, 1);
    }
    // Translate first, then scale: a point on the output becomes an offset
    // from the window's origin, and that offset is shrunk by the ratio of the
    // surface to the frame. QMatrix4x4 applies the operation added last
    // first, so the order here is the reverse of the order applied.
    QMatrix4x4 transformation;
    transformation.scale(float(presented.scale.x()), float(presented.scale.y()));
    transformation.translate(float(-presented.origin.x()), float(-presented.origin.y()));
    if (focused != presented.surface) {
        // KWin's hit test ended beside the surface, in the part of the output
        // the window is presented across but its surface does not cover: in
        // the window underneath, or in nothing at all.
        seat->notifyPointerEnter(presented.surface, position, transformation);
    } else if (seat->focusedPointerSurfaceTransformation() != transformation) {
        // Set by KWin when the surface gained focus or its window moved, or
        // stale from an earlier size of the surface.
        seat->setFocusedPointerSurfaceTransformation(transformation);
    }
    confirmEmulatedPosition(seat, presented, position, transformation);
    recordMapping(presented, position, transformation);
    m_surface = presented.surface;
    // Whose pointer this is now, when it is not the window KWin found: the
    // events for it are this filter's to deliver, or the filters between here
    // and KWin's forwarding would act on the window underneath.
    Window *claimed = input()->pointer()->focus() == presented.window ? nullptr : presented.window;
    if (claimed && m_claimed != claimed) {
        qCInfo(KWIN_UPSCALE) << "Pointer coverage claimed: window" << claimed->internalId()
                             << "position" << position << "client area" << presented.client << "scale" << presented.scale;
    }
    m_claimed = claimed;
    if (m_claimed) {
        engageLock(presented, position);
    }
    return presented.scale;
}

void UpscaleX11Input::recordMapping(const UpscalePresentedPointer &presented, const QPointF &position, const QMatrix4x4 &transformation)
{
    if (m_surface == presented.surface && m_transformation == transformation && m_client == presented.client) {
        return;
    }
    qCDebug(KWIN_UPSCALE) << "Pointer mapping: window" << presented.window->internalId() << "position" << position
                          << "origin" << presented.origin << "scale" << presented.scale << "client area" << presented.client;
    m_transformation = transformation;
    m_client = presented.client;
}

void UpscaleX11Input::refresh()
{
    if (input()) {
        apply(input()->pointer()->pos());
    }
}

// Delivers what a filter of KWin's own would otherwise deliver to the window
// under the pointer, which here is the wrong one. The seat sends it to the
// surface this filter focused, mapped by the transformation it set.
bool UpscaleX11Input::deliver(const std::function<void(SeatInterface *seat)> &send)
{
    SeatInterface *seat = m_claimed && waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat) {
        return false;
    }
    send(seat);
    return true;
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
    // Motion is left to KWin: it forwards it to the surface on the seat, which
    // is the presented one, and nothing between here and there moves a window
    // because the pointer passed over it.
    return false;
}

bool UpscaleX11Input::pointerButton(PointerButtonEvent *event)
{
    apply(event->position);
    // A press over the presented window is what makes it the active one, since
    // KWin's own click handling is about to be passed over.
    EffectWindow *claimed = m_claimed ? m_claimed->effectWindow() : nullptr;
    if (claimed && event->state == PointerButtonState::Pressed && effects && effects->activeWindow() != claimed) {
        effects->activateWindow(claimed);
    }
    return deliver([event](SeatInterface *seat) {
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerButton(event->nativeButton, event->state);
    });
}

bool UpscaleX11Input::pointerAxis(PointerAxisEvent *event)
{
    apply(event->position);
    return deliver([event](SeatInterface *seat) {
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerAxis(event->orientation, event->delta, event->deltaV120, event->source, event->inverted);
    });
}

} // namespace KWin
#endif
