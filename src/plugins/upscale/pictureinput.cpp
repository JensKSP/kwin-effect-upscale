/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pictureinput.h"

#include "eligibility.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "input_event.h"
#include "pointer_input.h"
#include "wayland/pointer.h"
#include "wayland/pointerconstraints_v1.h"
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"

#include <algorithm>

namespace KWin
{

UpscalePictureInput::UpscalePictureInput(std::function<EffectWindow *(Window *window)> scaled,
                                         std::function<EffectWindow *(const QPointF &position)> drawnAt)
    // Where the X11 filter runs: behind whatever may take the event away from
    // the window first, ahead of the filters that act on it.
    : InputEventFilter(InputFilterOrder::Order(InputFilterOrder::WindowAction - 1))
    , m_scaled(std::move(scaled))
    , m_drawnAt(std::move(drawnAt))
{
    if (input()) {
        input()->installInputEventFilter(this);
    }
}

UpscalePictureInput::~UpscalePictureInput()
{
    // Give the seat back while KWin's focus is still the one it was set for.
    m_scaled = nullptr;
    m_drawnAt = nullptr;
    if (input()) {
        apply(input()->pointer()->pos());
    }
}

// Where the picture of a window the effect scales lies on its output.
static QRectF pictureOf(EffectWindow *scaled)
{
    const UpscalePicture placed = upscalePictureOf(scaled);
    if (placed.sizing != UpscaleSizing::Supported) {
        return {};
    }
    const qreal pixels = scaled->screen()->scale();
    const QRectF frame = upscalePresentedFrame(scaled);
    return QRectF(frame.x() + (placed.x / pixels), frame.y() + (placed.y / pixels), placed.width / pixels, placed.height / pixels);
}

// The picture of the window the effect scales, where it has bars and the
// pointer is KWin's on that window; empty otherwise.
static QRectF letterboxedPicture(EffectWindow *scaled, Window *focus)
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat || !scaled || !focus || scaled->window() != focus || !focus->surface() || !focus->output()
        || seat->focusedPointerSurface() != focus->surface()) {
        return {};
    }
    const QRectF picture = pictureOf(scaled);
    const QRectF frame = focus->frameGeometry();
    const qreal pixels = focus->output()->scale();
    if (upscaleSamePixel(picture.width(), frame.width(), pixels) && upscaleSamePixel(picture.height(), frame.height(), pixels)) {
        return {};
    }
    return picture;
}

// A game that locks the pointer, as mouse look does, is given that lock by
// KWin only while KWin's own focus is on its surface, which follows where the
// cursor is in the window rather than in the picture the user sees. So the
// cursor is put inside the window once, which is what makes KWin take the
// lock; a locked pointer is neither shown nor moved afterwards. Only where
// KWin would take the lock at all: while the window is the active one.
static void engageLock(Window *window, const QPointF &position)
{
    LockedPointerV1Interface *lock = window->surface()->lockedPointer();
    const QRectF client = window->clientGeometry();
    if (!lock || lock->isLocked() || client.contains(position) || !effects || effects->activeWindow() != window->effectWindow()) {
        return;
    }
    input()->pointer()->warp(client.center());
}

// KWin suppresses motion at the position just entered, even where its own hit
// test entered the surface this filter had focused, with its own mapping,
// just before, as when the pointer moves from beside a drawn window onto it.
// The mapped position is confirmed before a click can follow.
static void confirmEnteredPosition(SeatInterface *seat, SurfaceInterface *surface, const QPointF &position,
                                   const QMatrix4x4 &transformation)
{
    LockedPointerV1Interface *lock = surface->lockedPointer();
    if (seat->pointer() && seat->pointerPos() == position && (!lock || !lock->isLocked())) {
        seat->pointer()->sendMotion(transformation.map(position));
    }
}

void UpscalePictureInput::giveBack(SeatInterface *seat, Window *focus, const QPointF &position)
{
    if (m_surface && seat->focusedPointerSurface() == m_surface) {
        if (focus && focus->surface() == m_surface) {
            seat->setFocusedPointerSurfaceTransformation(focus->inputTransformation());
        } else if (m_claimed && focus && focus->surface()) {
            seat->notifyPointerEnter(focus->surface(), position, focus->inputTransformation());
        } else if (m_claimed) {
            seat->notifyPointerLeave();
        }
    }
    m_surface = nullptr;
    m_claimed = nullptr;
    m_picture = QRectF();
}

QPointF UpscalePictureInput::apply(const QPointF &position)
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    Window *focus = input() ? input()->pointer()->focus() : nullptr;
    if (!seat) {
        return QPointF(1, 1);
    }
    // A window drawn over the output here is the pointer's wherever KWin found
    // nothing, or found what its picture covers. Asked of the window KWin
    // found, decoration included, where its focus goes to no window at all.
    EffectWindow *drawn = m_drawnAt ? m_drawnAt(position) : nullptr;
    Window *hover = input() ? input()->pointer()->hover() : nullptr;
    if (drawn && hover && hover != drawn->window() && !upscaleDrawnCovers(drawn, hover->effectWindow())) {
        drawn = nullptr;
    }
    Window *target = drawn ? drawn->window() : focus;
    const QRectF picture = drawn ? pictureOf(drawn) : letterboxedPicture(m_scaled ? m_scaled(focus) : nullptr, focus);
    // A confined drawn window is KWin's: it keeps the pointer inside the
    // window, in the surface's own coordinates and before any filter, so the
    // pointer passes one to one there, as for a presented X11 window.
    ConfinedPointerV1Interface *confinement = drawn && target->surface() ? target->surface()->confinedPointer() : nullptr;
    if (picture.isEmpty() || !target->surface() || (confinement && confinement->isConfined())) {
        giveBack(seat, focus, position);
        return QPointF(1, 1);
    }
    // The surface's own coordinates cover the window, and the game maps them
    // onto its buffer as though that filled it - a Wayland game itself, an X11
    // game in a mode of its own through Xwayland's emulation - so the picture
    // is scaled to the window. Translate first, then scale; QMatrix4x4
    // applies the operation added last first.
    const QRectF client = target->clientGeometry();
    const QPointF scale(client.width() / picture.width(), client.height() / picture.height());
    QMatrix4x4 transformation;
    transformation.scale(float(scale.x()), float(scale.y()));
    transformation.translate(float(-picture.x()), float(-picture.y()));
    if (seat->focusedPointerSurface() != target->surface()) {
        seat->notifyPointerEnter(target->surface(), position, transformation);
    } else if (seat->focusedPointerSurfaceTransformation() != transformation) {
        seat->setFocusedPointerSurfaceTransformation(transformation);
    }
    confirmEnteredPosition(seat, target->surface(), position, transformation);
    m_surface = target->surface();
    m_picture = picture;
    m_claimed = target == focus ? nullptr : target;
    if (m_claimed) {
        engageLock(m_claimed, position);
    }
    return scale;
}

bool UpscalePictureInput::deliver(const std::function<void(SeatInterface *seat)> &send)
{
    SeatInterface *seat = m_claimed && waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat) {
        return false;
    }
    send(seat);
    return true;
}

bool UpscalePictureInput::pointerMotion(PointerMotionEvent *event)
{
    const QPointF scale = apply(event->position);
    if (scale == QPointF(1, 1)) {
        return false;
    }
    // A confined pointer is kept on the picture: in a bar it would reach
    // nothing of the game, which the confinement promises it never does.
    // The motion is taken back and the pointer put at the picture's edge,
    // which arrives as a motion of its own.
    ConfinedPointerV1Interface *confinement = m_surface ? m_surface->confinedPointer() : nullptr;
    if (confinement && confinement->isConfined() && !m_picture.contains(event->position)) {
        input()->pointer()->warp(QPointF(std::clamp(event->position.x(), m_picture.left(), m_picture.right() - 1),
                                         std::clamp(event->position.y(), m_picture.top(), m_picture.bottom() - 1)));
        return true;
    }
    // Relative motion is counted in the surface's coordinates too.
    event->delta = QPointF(event->delta.x() * scale.x(), event->delta.y() * scale.y());
    event->deltaUnaccelerated = QPointF(event->deltaUnaccelerated.x() * scale.x(), event->deltaUnaccelerated.y() * scale.y());
    // Where the pointer is this filter's, so is its motion: over a decoration
    // the picture hides, KWin's decoration filter would take it. What KWin's
    // forwarding sends is sent here; a warp, which KWin's versions forward
    // differently, stays KWin's.
    if (m_claimed && !event->warp) {
        return deliver([event](SeatInterface *seat) {
            seat->setTimestamp(event->timestamp);
            seat->notifyPointerMotion(event->position);
            if (!event->delta.isNull()) {
                seat->relativePointerMotion(event->delta, event->deltaUnaccelerated, event->timestamp);
            }
        });
    }
    return false;
}

bool UpscalePictureInput::pointerButton(PointerButtonEvent *event)
{
    // Where KWin's focus moved without a motion, as a window mapping under
    // the pointer makes it, the press still lands on the picture.
    apply(event->position);
    // A press on a drawn window beside it makes it the active one, since
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

bool UpscalePictureInput::pointerAxis(PointerAxisEvent *event)
{
    apply(event->position);
    return deliver([event](SeatInterface *seat) {
        seat->setTimestamp(event->timestamp);
        seat->notifyPointerAxis(event->orientation, event->delta, event->deltaV120, event->source, event->inverted);
    });
}

} // namespace KWin
