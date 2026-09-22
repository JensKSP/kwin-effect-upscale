/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The effect's screen pass and what it reports: the displays and the question
// drawn after the chain has painted a frame, and the status the settings page
// and D-Bus read.

#include "eligibility.h"
#include "preparation.h"
#include "snapshot.h"
#include "upscale.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "input_event.h"

#include <KLocalizedString>
#include <QMouseEvent>
#include <QScopedValueRollback>

namespace KWin
{

EffectWindow *UpscaleEffect::displayed() const
{
    if (EffectWindow *scaled = candidate()) {
        return scaled;
    }
    // A refused fullscreen window is exactly the case that needs explaining,
    // so the display follows it. Ordinary desktop windows are left alone.
    EffectWindow *active = effects->activeWindow();
    return active && upscalePresentation(active) && !active->isDeleted() ? active : nullptr;
}

void UpscaleEffect::paintDisplay(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen)
{
    // A lock screen must not carry a report about what was running behind it,
    // and a display with nothing to show releases what it was holding.
    if (!m_display.enabled() || effects->isScreenLocked()) {
        m_display.hide();
        return;
    }
    EffectWindow *window = displayed();
    if (!window) {
        m_display.hide();
        return;
    }
    if (window->screen() != screen) {
        return;
    }
    m_display.measure(screen);
    m_display.countRepaint();
    if (m_display.wantsSnapshot(window)) {
        m_display.update(snapshot(window, &target), window);
    }
    m_display.paint(target, viewport, screen->geometryF());
}

UpscalePaintResult UpscaleEffect::paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask,
                                              const UpscaleRegion &region, UpscaleOutput *screen)
{
    const QScopedValueRollback painting(m_inPaint, true);
    const QScopedValueRollback output(m_paintOutput, screen);
    m_candidateCached = false;
    // Resolve this output's candidate now rather than leaving it to whoever
    // asks first. Resolving is where a window drawing at full size is asked
    // for a smaller buffer, and such a window is never eligible, so drawWindow
    // never asks for it; with the display switched off nothing else would, and
    // the request would never be made.
    candidate(nullptr, screen);
    // The display is drawn after the screen pass, which is after the scaler
    // captured the game's surface. That ordering is what keeps this text out
    // of the captured image and out of the enlargement.
#if UPSCALE_RENDER_DEVICE_API
    if (!effects->paintScreen(target, viewport, mask, region, screen)) {
        return false;
    }
    paintDisplay(target, viewport, screen);
    m_preparation->question().paint(target, viewport, screen);
    return true;
#else
    effects->paintScreen(target, viewport, mask, region, screen);
    paintDisplay(target, viewport, screen);
    m_preparation->question().paint(target, viewport, screen);
#endif
}

void UpscaleEffect::grabbedKeyboardEvent(QKeyEvent *event)
{
    m_preparation->question().key(event);
}

void UpscaleEffect::unfollowed(EffectWindow *window, const QSize &size)
{
    m_preparation->unfollowed(window, size);
}

QString UpscaleEffect::question() const
{
    return m_preparation->question().text();
}

QList<QRectF> UpscaleEffect::questionAnswers() const
{
    return m_preparation->question().answerAreas();
}

// The pointer, while a question intercepts it: hovering an answer selects it
// and releasing the left button over one chooses it.
#if UPSCALE_POINTER_EVENT_API
void UpscaleEffect::pointerMotion(PointerMotionEvent *event)
{
    m_preparation->question().pointerMoved(event->position);
}

void UpscaleEffect::pointerButton(PointerButtonEvent *event)
{
    if (event->button == Qt::LeftButton && event->state == PointerButtonState::Released) {
        m_preparation->question().pointerReleased(event->position);
    }
}
#else
void UpscaleEffect::windowInputMouseEvent(QEvent *event)
{
    if (event->type() != QEvent::MouseMove && event->type() != QEvent::MouseButtonRelease) {
        return;
    }
    const auto mouse = static_cast<QMouseEvent *>(event);
    if (event->type() == QEvent::MouseMove) {
        m_preparation->question().pointerMoved(mouse->globalPosition());
    } else if (mouse->button() == Qt::LeftButton) {
        m_preparation->question().pointerReleased(mouse->globalPosition());
    }
}
#endif

QString UpscaleEffect::build() const
{
    return m_build;
}

QString UpscaleEffect::status() const
{
    // The settings page may ask about any active window, not only a fullscreen
    // one, so it does not use the display's narrower choice.
    EffectWindow *window = candidate();
    if (!window) {
        window = effects->activeWindow();
    }
    if (!window) {
        return i18n("Inactive: %1", describeRefusal(UpscaleRefusal::NoWindow));
    }
    // The measurements live in the display, which is what follows one window
    // and one output for long enough to have them. This snapshot is built
    // fresh for the question and has none, so it borrows them rather than
    // reporting a running game with its frame times missing.
    UpscaleSnapshot state = snapshot(window, nullptr);
    m_display.applyMeasurements(state, window, window->screen());
    return upscaleStatusText(state);
}

} // namespace KWin
