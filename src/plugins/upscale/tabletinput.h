/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPointF>

// KWin's input filters take a pen's events as structs of their own from 6.6
// on and as a QTabletEvent before. The top-level build says which; KWin's own
// build, which has only the newer, finds the default here.
#ifndef UPSCALE_TABLET_EVENTS
#define UPSCALE_TABLET_EVENTS 1
#endif

namespace KWin
{
class InputDevice;
class InputDeviceTabletTool;
class SurfaceInterface;
#if UPSCALE_TABLET_EVENTS
struct TabletToolProximityEvent;
struct TabletToolAxisEvent;
struct TabletToolTipEvent;
#else
class TabletEvent;
#endif

/** What a pen does, in the order KWin's forwarding tells a client of it. */
enum class UpscalePenAction {
    EnterProximity,
    LeaveProximity,
    Motion,
    Press,
    Release,
};

/** One event of a pen, whichever form KWin gave it in. */
struct UpscalePen
{
    QPointF position;
    UpscalePenAction action = UpscalePenAction::Motion;
    InputDeviceTabletTool *tool = nullptr;
    InputDevice *device = nullptr;
    qreal pressure = 0;
    qreal xTilt = 0;
    qreal yTilt = 0;
    qreal rotation = 0;
    qreal distance = 0;
    qreal slider = 0;
    // Milliseconds, as the tablet protocol counts them.
    quint32 time = 0;
};

#if UPSCALE_TABLET_EVENTS
UpscalePen upscalePen(const TabletToolProximityEvent &event);
UpscalePen upscalePen(const TabletToolAxisEvent &event);
UpscalePen upscalePen(const TabletToolTipEvent &event);
#else
UpscalePen upscalePen(const TabletEvent &event);
#endif

/**
 * Tell the client of @p surface of @p pen at @p local, the point of the
 * surface the picture shows where the pen is, as KWin's forwarding tells the
 * client of the window under the pen. False where that client takes no
 * tablet, which KWin then handles itself.
 */
bool upscaleDeliverPen(const UpscalePen &pen, SurfaceInterface *surface, const QPointF &local);

} // namespace KWin
