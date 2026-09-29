/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "input.h"

#include <QMatrix4x4>
#include <QPointer>
#include <QRectF>

#include <functional>

namespace KWin
{
class EffectWindow;
class SeatInterface;
class SurfaceInterface;
class Window;

/**
 * Maps pointer input onto the picture of a window the effect scales, where
 * the picture and the window's surface differ.
 *
 * A fullscreen Wayland game's surface covers its output, and KWin hands it
 * pointer positions across the whole of it, which the game maps onto its
 * buffer as though that were stretched over the surface. An X11 game that set
 * a mode of its own is the same case one step later: Xwayland's emulation
 * scales the surface over the output and maps positions back. Where the effect
 * lays the picture smaller than the surface - another aspect ratio fitted in,
 * or enlarged by a whole factor - the game would see the pointer where the
 * picture is not. This filter gives the seat the transformation from the
 * picture to the surface instead, scales relative motion by the same factor,
 * and keeps a confined pointer out of the bars, where it would reach nothing
 * of the game.
 *
 * A plain window its program sized to the smaller screen it was told is drawn
 * over its whole output (see upscaleDrawnOverOutput()), and mapped the same
 * way from its picture there. Beside the window, where KWin's hit test finds
 * what the picture covers - the desktop, a panel, a decoration, nothing at all
 * - this filter focuses the window's surface on the seat itself and delivers
 * the pointer's motion, buttons and wheel to it, which the filters after it
 * would otherwise act on for the window KWin found, much as UpscaleX11Input
 * does for an X11 window it presents.
 * An X11 window the effect asked for a size is UpscaleX11Input's.
 */
class UpscalePictureInput : public InputEventFilter
{
public:
    /**
     * @p scaled answers the window the effect scales, if it is the one given;
     * @p drawnAt the window drawn over the output at a position, if any.
     */
    UpscalePictureInput(std::function<EffectWindow *(Window *window)> scaled,
                        std::function<EffectWindow *(const QPointF &position)> drawnAt);
    ~UpscalePictureInput() override;

    bool pointerMotion(PointerMotionEvent *event) override;
    bool pointerButton(PointerButtonEvent *event) override;
    bool pointerAxis(PointerAxisEvent *event) override;

private:
    // Sets the seat's focus and transformation for the picture under
    // @p position, and answers the scale relative motion takes, which is one
    // where nothing applies.
    QPointF apply(const QPointF &position);
    // Gives the seat back to what KWin's own hit test found.
    void giveBack(SeatInterface *seat, Window *focus, const QPointF &position);
    // Sends what KWin's forwarding would have sent to the window it found,
    // and answers whether it did, which is whether the event is handled.
    bool deliver(const std::function<void(SeatInterface *seat)> &send);

    std::function<EffectWindow *(Window *window)> m_scaled;
    std::function<EffectWindow *(const QPointF &position)> m_drawnAt;
    // The surface whose transformation this filter set, to give back.
    QPointer<SurfaceInterface> m_surface;
    // The picture the pointer was last mapped from.
    QRectF m_picture;
    // The window this filter focused where KWin found another, whose events
    // are therefore this filter's to deliver.
    QPointer<Window> m_claimed;
};

} // namespace KWin
