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
#include "windowidentity.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"

#include <algorithm>

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
    if (!settings.acts()) {
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

// Whether Auto has a Wayland window to ask, or is waiting for one to answer.
// KWin calls only an active effect's paint hooks, and those are where Auto
// asks and counts its patience; without this, a game drawing at full size
// would be asked only while something else happened to keep the effect
// active - an on-screen display, say - and never otherwise.
bool UpscaleEffect::autoWaiting() const
{
    if (m_waylandScale->asking()) {
        return true;
    }
    const QList<UpscaleOutput *> outputs = effects->screens();
    return std::ranges::any_of(outputs, [this](UpscaleOutput *output) {
        EffectWindow *window = upscaleWindowAwaitingBuffer(output);
        if (!window || !window->isWaylandClient() || m_waylandScale->known(window->window())) {
            return false;
        }
        const UpscaleApplication *claimed = upscaleApplicationForWindow(window->window());
        return autoRatio(window, claimed, upscaleResolveSettings(claimed)) < 1.0;
    });
}

// Auto's Wayland half, run where the candidate was resolved. The window asked
// is the one this output would scale once its buffer allowed it: the
// candidate when there is one, and otherwise the window that qualifies in
// every respect but its buffer - which is the window Auto exists for.
void UpscaleEffect::askForSmallerBuffer(UpscaleOutput *output, EffectWindow *candidate,
                                        const UpscaleApplication *claimed) const
{
    EffectWindow *window = candidate ? candidate : upscaleWindowAwaitingBuffer(output);
    m_waylandScale->releaseOthers(output, window);
    if (!window || !window->isWaylandClient()) {
        return;
    }
    const UpscaleApplication *asked = window == candidate ? claimed : upscaleApplicationForWindow(window->window());
    m_waylandScale->request(window, autoRatio(window, asked, upscaleResolveSettings(asked)));
}

} // namespace KWin
