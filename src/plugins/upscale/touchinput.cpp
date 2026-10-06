/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "touchinput.h"

#include "wayland_server.h"

namespace KWin
{

static QPointF mapped(const QPointF &position, const QPointF &origin, const QPointF &scale)
{
    return QPointF((position.x() - origin.x()) * scale.x(), (position.y() - origin.y()) * scale.y());
}

void UpscaleTouchDelivery::down(SurfaceInterface *surface, qint32 id, const QPointF &position, std::chrono::microseconds time,
                                const QPointF &origin, const QPointF &scale)
{
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (!seat || !surface) {
        return;
    }
    seat->setTimestamp(time);
    // The seat subtracts this offset from the position it is given, which
    // leaves the point the picture shows.
    TouchPoint *point = seat->notifyTouchDown(surface, position - mapped(position, origin, scale), id, position);
    m_touches.insert(id, {point, origin, scale});
}

void UpscaleTouchDelivery::swallow(qint32 id)
{
    m_touches.insert(id, {});
}

bool UpscaleTouchDelivery::motion(qint32 id, const QPointF &position, std::chrono::microseconds time)
{
    const auto touch = m_touches.constFind(id);
    if (touch == m_touches.cend()) {
        return false;
    }
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (touch->point && seat) {
        // As TouchPoint::setSurfacePosition() sets them, which KWin does not
        // export to plugins built outside its tree.
        const QPointF offset = position - mapped(position, touch->origin, touch->scale);
        touch->point->offset = offset;
        touch->point->transformation = QMatrix4x4();
        touch->point->transformation.translate(float(-offset.x()), float(-offset.y()));
        seat->setTimestamp(time);
        seat->notifyTouchMotion(id, position);
    }
    return true;
}

bool UpscaleTouchDelivery::up(qint32 id, std::chrono::microseconds time)
{
    const auto touch = m_touches.constFind(id);
    if (touch == m_touches.cend()) {
        return false;
    }
    SeatInterface *seat = waylandServer() ? waylandServer()->seat() : nullptr;
    if (touch->point && seat) {
        seat->setTimestamp(time);
        seat->notifyTouchUp(id);
    }
    m_touches.erase(touch);
    return true;
}

void UpscaleTouchDelivery::cancel()
{
    m_touches.clear();
}

} // namespace KWin
