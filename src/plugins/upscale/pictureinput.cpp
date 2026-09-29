/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pictureinput.h"

#include "eligibility.h"

#include "effect/effectwindow.h"
#include "input_event.h"
#include "pointer_input.h"
#include "wayland/pointerconstraints_v1.h"
#include "wayland/seat.h"
#include "wayland/surface.h"
#include "wayland_server.h"
#include "window.h"

#include <algorithm>

namespace KWin
{

UpscalePictureInput::UpscalePictureInput(std::function<EffectWindow *(Window *window)> scaled)
    // Where the X11 filter runs: behind whatever may take the event away from
    // the window first, ahead of the filters that act on it.
    : InputEventFilter(InputFilterOrder::Order(InputFilterOrder::WindowAction - 1))
    , m_scaled(std::move(scaled))
{
    if (input()) {
        input()->installInputEventFilter(this);
    }
}

UpscalePictureInput::~UpscalePictureInput()
{
    // Give the transformation back while KWin's focus is still the one it
    // was set for.
    m_scaled = nullptr;
    if (input()) {
        apply();
    }
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
    const UpscalePicture placed = upscalePictureOf(scaled);
    if (placed.sizing != UpscaleSizing::Supported) {
        return {};
    }
    const qreal pixels = focus->output()->scale();
    const QRectF frame = focus->frameGeometry();
    const QRectF picture(frame.x() + (placed.x / pixels), frame.y() + (placed.y / pixels), placed.width / pixels, placed.height / pixels);
    if (upscaleSamePixel(picture.width(), frame.width(), pixels) && upscaleSamePixel(picture.height(), frame.height(), pixels)) {
        return {};
    }
    return picture;
}

QPointF UpscalePictureInput::apply()
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    Window *focus = input() ? input()->pointer()->focus() : nullptr;
    const QRectF picture = letterboxedPicture(m_scaled ? m_scaled(focus) : nullptr, focus);
    if (picture.isEmpty()) {
        // Nothing of this filter's any more: give back what KWin had set.
        if (m_surface && seat && focus && focus->surface() == m_surface && seat->focusedPointerSurface() == m_surface) {
            seat->setFocusedPointerSurfaceTransformation(focus->inputTransformation());
        }
        m_surface = nullptr;
        return QPointF(1, 1);
    }
    // The surface's own coordinates cover the frame, and the game maps them
    // onto its buffer as though that filled it - a Wayland game itself, an X11
    // game in a mode of its own through Xwayland's emulation - so the picture
    // is scaled up to the frame. Translate first, then scale; QMatrix4x4
    // applies the operation added last first.
    const QRectF frame = focus->frameGeometry();
    const QPointF scale(frame.width() / picture.width(), frame.height() / picture.height());
    QMatrix4x4 transformation;
    transformation.scale(float(scale.x()), float(scale.y()));
    transformation.translate(float(-picture.x()), float(-picture.y()));
    if (seat->focusedPointerSurfaceTransformation() != transformation) {
        seat->setFocusedPointerSurfaceTransformation(transformation);
    }
    m_surface = focus->surface();
    return scale;
}

bool UpscalePictureInput::pointerMotion(PointerMotionEvent *event)
{
    const QPointF scale = apply();
    if (scale == QPointF(1, 1)) {
        return false;
    }
    // A confined pointer is kept on the picture: in a bar it would reach
    // nothing of the game, which the confinement promises it never does.
    // The motion is taken back and the pointer put at the picture's edge,
    // which arrives as a motion of its own.
    ConfinedPointerV1Interface *confinement = m_surface ? m_surface->confinedPointer() : nullptr;
    Window *focus = input()->pointer()->focus();
    const QRectF picture = letterboxedPicture(m_scaled ? m_scaled(focus) : nullptr, focus);
    if (confinement && confinement->isConfined() && !picture.contains(event->position)) {
        input()->pointer()->warp(QPointF(std::clamp(event->position.x(), picture.left(), picture.right() - 1),
                                         std::clamp(event->position.y(), picture.top(), picture.bottom() - 1)));
        return true;
    }
    // Relative motion is counted in the surface's coordinates too.
    event->delta = QPointF(event->delta.x() * scale.x(), event->delta.y() * scale.y());
    event->deltaUnaccelerated = QPointF(event->deltaUnaccelerated.x() * scale.x(), event->deltaUnaccelerated.y() * scale.y());
    return false;
}

bool UpscalePictureInput::pointerButton(PointerButtonEvent *event)
{
    // Where KWin's focus moved without a motion, as a window mapping under
    // the pointer makes it, the press still lands on the picture.
    Q_UNUSED(event)
    apply();
    return false;
}

} // namespace KWin
