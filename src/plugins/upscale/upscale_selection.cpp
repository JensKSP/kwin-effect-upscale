/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "eligibility.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"

namespace KWin
{

void UpscaleEffect::watchWindow(EffectWindow *window)
{
    connect(window, &QObject::destroyed, this, [this, window]() {
        m_renderedInputs.remove(window);
    });
    if (Window *internal = window->window()) {
        connect(internal, &Window::outputChanged, this, [this, window]() {
            m_unsupportedColors.removeAll(window);
            m_candidateCached = false;
            window->addRepaintFull();
        });
        // An application ID can arrive after its window does, and an X11
        // client may replace it later. Repaint so that what is shown on
        // screen follows the identity rather than the first guess at it.
        connect(internal, &Window::windowClassChanged, this, [window]() {
            window->addRepaintFull();
        });
    }
    connect(window, &EffectWindow::windowDamaged, this, [this, window]() {
        if (windowRefusal(window) == UpscaleRefusal::None) {
            // EASU and RCAS read neighbouring pixels. Full-window damage is
            // conservative and follows client commits, never a repaint timer.
            window->addRepaintFull();
        }
        if (m_display.enabled() && !effects->isScreenLocked()) {
            m_display.countClientUpdate(window);
        }
    });
}

void UpscaleEffect::watchOutput(UpscaleOutput *output)
{
    const auto changed = [this, output]() {
        // Colour refusal is valid only for the output configuration that was
        // painted. Retry on a real change, never by repainting in a loop.
        m_unsupportedColors.removeIf([output](const QPointer<EffectWindow> &window) {
            return !window || window->screen() == output;
        });
        m_candidateCached = false;
        effects->addRepaintFull();
    };
    connect(output, &UpscaleOutput::changed, this, changed);
#if UPSCALE_REGION_API
    connect(output, &UpscaleOutput::blendingColorChanged, this, changed);
#else
    connect(output, &UpscaleOutput::colorDescriptionChanged, this, changed);
#endif
}

EffectWindow *UpscaleEffect::candidate(UpscaleRefusal *refusal, UpscaleOutput *output) const
{
    if (!output && m_inPaint) {
        output = m_paintOutput;
    }
    if (!output) {
        // A status/activation query has no paint output. Prefer the active
        // output, then look for work elsewhere; a refusal on one must never
        // disable a candidate on another.
        EffectWindow *selected = findCandidate(refusal, effects->activeScreen());
        for (UpscaleOutput *screen : effects->screens()) {
            if (!selected && screen != effects->activeScreen()) {
                selected = findCandidate(nullptr, screen);
            }
        }
        return selected;
    }
    if (!m_inPaint) {
        return findCandidate(refusal, output);
    }
    if (!m_candidateCached || m_candidateOutput != output) {
        m_candidate = findCandidate(&m_candidateRefusal, output);
        m_candidateOutput = output;
        m_candidateCached = true;
    }
    if (refusal) {
        *refusal = m_candidateRefusal;
    }
    return m_candidate;
}

EffectWindow *UpscaleEffect::findCandidate(UpscaleRefusal *refusal, UpscaleOutput *output) const
{
    const auto refuse = [refusal](UpscaleRefusal reason) -> EffectWindow * {
        if (refusal) {
            *refusal = reason;
        }
        return nullptr;
    };
    if (!m_enabled) {
        return refuse(UpscaleRefusal::Disabled);
    }
    if (m_failed) {
        return refuse(UpscaleRefusal::ResourceFailure);
    }
    if (effects->isScreenLocked()) {
        return refuse(UpscaleRefusal::ScreenLocked);
    }
    if (effects->activeFullScreenEffect()) {
        return refuse(UpscaleRefusal::OtherFullScreenEffect);
    }
    EffectWindow *selected = nullptr;
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        if (window->screen() == output && windowRefusal(window) == UpscaleRefusal::None) {
            if (selected) {
                return refuse(UpscaleRefusal::SeveralCandidates);
            }
            selected = window;
        }
    }
    if (!selected) {
        // Nothing here is eligible, so the interesting answer is why the
        // window the user is looking at is not. Asking costs one more pass
        // over the conditions and happens only for a caller that wants it.
        EffectWindow *active = effects->activeWindow();
        return refuse(active && active->screen() == output ? windowRefusal(active) : UpscaleRefusal::NoWindow);
    }
    if (m_unsupportedColors.contains(selected)) {
        return refuse(UpscaleRefusal::UnsupportedColors);
    }
    if (refusal) {
        *refusal = UpscaleRefusal::None;
    }
    return selected;
}

} // namespace KWin
