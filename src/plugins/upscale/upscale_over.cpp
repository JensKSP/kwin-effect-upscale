/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A window the effect draws over its whole output although it covers only part
// of it: a window sized to the smaller screen mode it was told, or a
// fullscreen window resized by Auto. KWin clips a window to its own
// rectangle and repaints only where something changed, so while such a window
// is drawn its output is painted whole, window by window from the bottom, as
// for an effect that transforms windows. What the picture covers is left out,
// as upscaleDrawnCovers() decides; the pointer follows in UpscalePictureInput.

#include "upscale.h"

#include "eligibility.h"
#include "modeoverride.h"
#include "waylandscale.h"

#include "effect/effecthandler.h"
#include "wayland/surface.h"
#include "window.h"

#include <algorithm>

namespace KWin
{

void UpscaleEffect::shareToldModes()
{
    upscaleSetResizedGeometry([this](const Window *window) {
        return m_waylandScale->resizedGeometry(window);
    });
    upscaleSetToldMode([this](const EffectWindow *window) {
        const Window *internal = window->window();
        return internal && internal->surface() && internal->output()
            ? m_modeOverride->advertised(internal->surface()->client(), internal->output()->name())
            : QSize();
    });
}

void UpscaleEffect::preparePaintArea(ScreenPrePaintData &data)
{
    UpscaleOutput *output = data.screen;
    EffectWindow *drawn = output ? candidate(nullptr, output) : nullptr;
    // A window that starts being enlarged changes its whole picture at once,
    // and one that stops gives the output back: either frame is painted whole,
    // whatever the commit that caused it damaged. Decided here, once a frame,
    // rather than by asking at every commit of every window whether it is now
    // eligible, which a client at 18 000 frames a second paid for.
    const bool switched = m_enlarged.value(output) != drawn;
    if (drawn) {
        m_enlarged.insert(output, drawn);
    } else {
        m_enlarged.remove(output);
    }
    // The frame after the last one drawn over is painted whole as well, to
    // take the picture off the rest of the output.
    const bool wasDrawn = m_drawnOver.remove(output) > 0;
    if (drawn && upscaleDrawnOverOutput(drawn)) {
        m_drawnOver.insert(output, drawn);
    }
    if (switched || wasDrawn || m_drawnOver.contains(output)) {
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }
}

bool UpscaleEffect::enlarged(const EffectWindow *window) const
{
    return std::ranges::any_of(m_enlarged, [window](const QPointer<EffectWindow> &shown) {
        return shown == window;
    });
}

bool UpscaleEffect::coveredByDrawn(EffectWindow *window) const
{
    EffectWindow *drawn = m_inPaint ? m_drawnOver.value(m_paintOutput) : nullptr;
    return upscaleDrawnCovers(drawn, window);
}

EffectWindow *UpscaleEffect::drawnAt(const QPointF &position) const
{
    UpscaleOutput *output = effects->screenAt(position.toPoint());
    EffectWindow *drawn = output ? m_drawnOver.value(output) : nullptr;
    return drawn && drawn->screen() == output && upscaleDrawnOverOutput(drawn) ? drawn : nullptr;
}

// The render-device API's own prePaintScreen() is in upscale.cpp.
#if UPSCALE_PREPAINT_PRESENT_TIME
void UpscaleEffect::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    preparePaintArea(data);
    effects->prePaintScreen(data, presentTime);
}
#elif !UPSCALE_RENDER_DEVICE_API
void UpscaleEffect::prePaintScreen(ScreenPrePaintData &data)
{
    preparePaintArea(data);
    effects->prePaintScreen(data);
}
#endif

} // namespace KWin
