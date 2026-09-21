/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Auto's Wayland half, kept apart from upscale.cpp so that the effect's paint
// path and the decision about what to ask a client for can each be read on
// their own - and because upscale.cpp would otherwise pass the size limit.

#include "upscale.h"

#include "application.h"
#include "eligibility.h"
#include "resolution.h"
#include "settings.h"
#include "waylandscale.h"

#include "effect/effectwindow.h"
#include "window.h"

namespace KWin
{

// The fraction of its output to ask this window to render, or one to ask for
// nothing and give back anything asked before.
//
// The advertisement methods are not reachable from here: they were made when
// the client bound the output, long before this window existed. What is
// reachable is the preferred fractional scale, which is sent per surface and
// can be taken back, so this is where Auto asks and where it stops asking.
static double autoRatio(EffectWindow *window, const UpscaleApplication *claimed, const UpscaleSettings &settings)
{
    if (!settings.acts() || !settings.switchedOn(UpscaleSetting::ResolutionControl)) {
        return 1.0;
    }
    const UpscalePresentation presentation = upscalePresentationOf(window);
    // Never a window the user sized. Its destination is its own size, so a
    // smaller buffer leaves no gap to enlarge into, and holding the frame
    // while the client renders below it is not something this effect does.
    if (upscaleIsWindowed(presentation)) {
        return 1.0;
    }
    // A profile that names an advertisement was answered at bind or not at
    // all. Only Auto is this mechanism's to act on, and an unmeasured slot is
    // Auto, which is the case that matters: a game nobody has measured. With
    // no profile, the global profile's own answer applies, which is Off unless
    // a person set it, so nothing is tried on an unmeasured program by default.
    const auto slot = std::size_t(presentation);
    const UpscaleMethod method = claimed ? claimed->methods[slot] : upscaleGlobalMethods()[slot];
    UpscaleOutput *output = window->screen();
    if (method != UpscaleMethod::Auto || !output) {
        return 1.0;
    }
    const QSize pixels = output->pixelSize();
    if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, settings.value(UpscaleSetting::MinimumPixels))) {
        return 1.0;
    }
    // resolutionRatio() answers Native with one, which is also the answer for
    // asking nothing, so the opt-out needs no test of its own.
    return resolutionRatio(settings.resolution(), settings.value(UpscaleSetting::Percentage));
}

// Auto's Wayland half, run where the candidate was resolved.
void UpscaleEffect::askForSmallerBuffer(EffectWindow *window, const UpscaleApplication *claimed) const
{
    if (!window || !window->isWaylandClient()) {
        return;
    }
    m_waylandScale->request(window, autoRatio(window, claimed, m_settings));
}

} // namespace KWin
