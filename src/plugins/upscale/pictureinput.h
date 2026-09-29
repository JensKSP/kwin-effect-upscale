/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "input.h"

#include <QMatrix4x4>
#include <QPointer>

#include <functional>

namespace KWin
{
class EffectWindow;
class SurfaceInterface;
class Window;

/**
 * Maps pointer input onto the picture of a window that has bars.
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
 * of the game. An X11 window the effect asked for a size is UpscaleX11Input's.
 */
class UpscalePictureInput : public InputEventFilter
{
public:
    /** @p scaled answers the window the effect scales, if it is the one given. */
    explicit UpscalePictureInput(std::function<EffectWindow *(Window *window)> scaled);
    ~UpscalePictureInput() override;

    bool pointerMotion(PointerMotionEvent *event) override;
    bool pointerButton(PointerButtonEvent *event) override;

private:
    // Sets the seat's transformation for the picture the pointer is on, and
    // answers the scale relative motion takes, one where nothing applies.
    QPointF apply();

    std::function<EffectWindow *(Window *window)> m_scaled;
    // The surface whose transformation this filter set, to give back.
    QPointer<SurfaceInterface> m_surface;
};

} // namespace KWin
