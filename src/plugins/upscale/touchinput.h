/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wayland/seat.h"

#include <QHash>
#include <QPointF>
#include <QPointer>

#include <chrono>

// Whether an input filter is handed touch as event structures, as KWin does
// after 6.3. The build compiles KWin's own declaration to find out; inside
// KWin's tree the current header decides.
#ifndef UPSCALE_TOUCH_EVENTS
#define UPSCALE_TOUCH_EVENTS 1
#endif

namespace KWin
{

class SurfaceInterface;

/**
 * Touch on a picture the effect presents, delivered to its window's surface at
 * the point the picture shows there, as the pointer is. The seat moves a touch
 * point by an offset alone, so the offset is set again at every motion to the
 * one that lands the touch where the picture shows it.
 */
class UpscaleTouchDelivery
{
public:
    /** A touch going down on @p surface, mapped as (position - @p origin) scaled by @p scale. */
    void down(SurfaceInterface *surface, qint32 id, const QPointF &position, std::chrono::microseconds time,
              const QPointF &origin, const QPointF &scale);
    /** A touch going down where it reaches nothing of the game: it stays this delivery's, and nobody's. */
    void swallow(qint32 id);
    /** Whether the touch is this delivery's; its motion is then delivered. */
    bool motion(qint32 id, const QPointF &position, std::chrono::microseconds time);
    /** Whether the touch is this delivery's; it is then lifted. */
    bool up(qint32 id, std::chrono::microseconds time);
    /** Forgets every touch, as KWin cancels them all. */
    void cancel();

private:
    struct Touch
    {
        // Null for a touch swallowed or one the seat let go of.
        QPointer<TouchPoint> point;
        QPointF origin;
        QPointF scale;
    };
    QHash<qint32, Touch> m_touches;
};

} // namespace KWin
