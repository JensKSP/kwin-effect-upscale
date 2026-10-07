/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tabletinput.h"

#include "input_event.h"
#include "wayland/surface.h"
#include "wayland/tablet_v2.h"
#include "wayland_server.h"

namespace KWin
{

#if UPSCALE_TABLET_EVENTS
static quint32 milliseconds(std::chrono::microseconds time)
{
    return quint32(std::chrono::duration_cast<std::chrono::milliseconds>(time).count());
}

UpscalePen upscalePen(const TabletToolProximityEvent &event)
{
    const auto action = event.type == TabletToolProximityEvent::EnterProximity ? UpscalePenAction::EnterProximity : UpscalePenAction::LeaveProximity;
    return {.position = event.position,
            .action = action,
            .tool = event.tool,
            .device = event.device,
            .xTilt = event.xTilt,
            .yTilt = event.yTilt,
            .rotation = event.rotation,
            .distance = event.distance,
            .slider = event.sliderPosition,
            .time = milliseconds(event.timestamp)};
}

UpscalePen upscalePen(const TabletToolAxisEvent &event)
{
    return {.position = event.position,
            .action = UpscalePenAction::Motion,
            .tool = event.tool,
            .device = event.device,
            .pressure = event.pressure,
            .xTilt = event.xTilt,
            .yTilt = event.yTilt,
            .rotation = event.rotation,
            .distance = event.distance,
            .slider = event.sliderPosition,
            .time = milliseconds(event.timestamp)};
}

UpscalePen upscalePen(const TabletToolTipEvent &event)
{
    return {.position = event.position,
            .action = event.type == TabletToolTipEvent::Press ? UpscalePenAction::Press : UpscalePenAction::Release,
            .tool = event.tool,
            .device = event.device,
            .pressure = event.pressure,
            .xTilt = event.xTilt,
            .yTilt = event.yTilt,
            .rotation = event.rotation,
            .distance = event.distance,
            .slider = event.sliderPosition,
            .time = milliseconds(event.timestamp)};
}
#else
UpscalePen upscalePen(const TabletEvent &event)
{
    UpscalePenAction action = UpscalePenAction::Motion;
    switch (event.type()) {
    case QEvent::TabletEnterProximity:
        action = UpscalePenAction::EnterProximity;
        break;
    case QEvent::TabletLeaveProximity:
        action = UpscalePenAction::LeaveProximity;
        break;
    case QEvent::TabletPress:
        action = UpscalePenAction::Press;
        break;
    case QEvent::TabletRelease:
        action = UpscalePenAction::Release;
        break;
    default:
        break;
    }
    return {.position = event.globalPosition(),
            .action = action,
            .tool = event.tool(),
            .device = event.device(),
            .pressure = event.pressure(),
            .xTilt = event.xTilt(),
            .yTilt = event.yTilt(),
            .rotation = event.rotation(),
            .distance = event.z(),
            .time = quint32(event.timestamp())};
}
#endif

bool upscaleDeliverPen(const UpscalePen &pen, SurfaceInterface *surface, const QPointF &local)
{
    TabletManagerV2Interface *manager = waylandServer() ? waylandServer()->tabletManagerV2() : nullptr;
    TabletSeatV2Interface *seat = manager ? manager->seat(waylandServer()->seat()) : nullptr;
    TabletToolV2Interface *tool = seat && pen.tool ? seat->tool(pen.tool) : nullptr;
    TabletV2Interface *tablet = seat && pen.device ? seat->tablet(pen.device) : nullptr;
    if (!tool || !tablet || !surface) {
        return false;
    }
    const auto [target, position] = surface->mapToInputSurface(local);
    tool->setCurrentSurface(target);
    if (!tool->isClientSupported() || !tablet->isSurfaceSupported(target)) {
        return false;
    }
    switch (pen.action) {
    case UpscalePenAction::EnterProximity:
        tool->sendProximityIn(tablet);
        tool->sendMotion(position);
        break;
    case UpscalePenAction::LeaveProximity:
        tool->sendProximityOut();
        break;
    case UpscalePenAction::Motion:
        tool->sendMotion(position);
        break;
    case UpscalePenAction::Press:
        tool->sendMotion(position);
        tool->sendDown();
        break;
    case UpscalePenAction::Release:
        tool->sendUp();
        break;
    }
    // The axes KWin sends with each event, where the pen has them: KWin 6.6
    // sends no pressure with a proximity, and KWin 6.3 sends no slider.
#if UPSCALE_TABLET_EVENTS
    const bool pressure = pen.action != UpscalePenAction::EnterProximity && pen.action != UpscalePenAction::LeaveProximity;
#else
    const bool pressure = true;
#endif
    if (pressure && tool->hasCapability(TabletToolV2Interface::Pressure)) {
        tool->sendPressure(pen.pressure);
    }
    if (tool->hasCapability(TabletToolV2Interface::Tilt)) {
        tool->sendTilt(pen.xTilt, pen.yTilt);
    }
    if (tool->hasCapability(TabletToolV2Interface::Rotation)) {
        tool->sendRotation(pen.rotation);
    }
    if (tool->hasCapability(TabletToolV2Interface::Distance)) {
        tool->sendDistance(pen.distance);
    }
#if UPSCALE_TABLET_EVENTS
    if (tool->hasCapability(TabletToolV2Interface::Slider)) {
        tool->sendSlider(pen.slider);
    }
#endif
    tool->sendFrame(pen.time);
    return true;
}

} // namespace KWin
