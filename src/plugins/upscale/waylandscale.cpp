/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "waylandscale.h"

#include "eligibility.h"

#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "window.h"

#include <cmath>

namespace KWin
{

// How many frames a client is given to act on the hint before it is taken
// back. A client that honours it resizes on the configure that carries the
// scale, so this is generous rather than finely judged; what it must not be
// is unbounded, because a client that never answers would otherwise keep a
// scale it is not using and the status would keep claiming a pending request.
static constexpr int patienceInFrames = 30;

UpscaleWaylandScale::UpscaleWaylandScale(QObject *parent)
    : QObject(parent)
{
}

UpscaleWaylandScale::~UpscaleWaylandScale()
{
    releaseAll();
}

void UpscaleWaylandScale::apply(Window *window, const Request &request)
{
    // KWin reapplies the output's own scale whenever the window changes
    // output or that output's scale changes, so a value set once is silently
    // undone. Re-asserting on the signal is what keeps the request standing;
    // without it this works until the moment anything touches the window.
    window->setNextTargetScale(request.original * request.ratio);
}

void UpscaleWaylandScale::observe(Window *window)
{
    connect(window, &Window::nextTargetScaleChanged, this, [this, window]() {
        const auto entry = m_requests.find(window);
        if (entry == m_requests.end()) {
            return;
        }
        const double wanted = entry->original * entry->ratio;
        if (std::abs(window->nextTargetScale() - wanted) > 0.001) {
            apply(window, *entry);
        }
    });
    connect(window, &Window::closed, this, [this, window]() {
        m_requests.remove(window);
    });
}

void UpscaleWaylandScale::request(EffectWindow *effectWindow, double ratio)
{
    Window *window = effectWindow ? effectWindow->window() : nullptr;
    if (!window || !effectWindow->isWaylandClient()) {
        return;
    }
    if (ratio >= 1.0 || ratio <= 0.0) {
        release(window);
        return;
    }
    auto entry = m_requests.find(window);
    if (entry == m_requests.end()) {
        Request fresh;
        // The scale the window had before anything was asked of it. Restoring
        // means putting this back, not assuming the output's current value,
        // because the output may have changed since.
        fresh.original = window->nextTargetScale();
        fresh.ratio = ratio;
        entry = m_requests.insert(window, fresh);
        observe(window);
        apply(window, *entry);
        return;
    }
    if (std::abs(entry->ratio - ratio) > 0.001) {
        // A different wish replaces the one standing, and the client is given
        // its patience again: it is being asked a new question.
        entry->ratio = ratio;
        entry->frames = 0;
        entry->answered = false;
        apply(window, *entry);
        return;
    }
    if (entry->answered) {
        return;
    }

    SurfaceItem *surface = effectWindow->windowItem() ? effectWindow->windowItem()->surfaceItem() : nullptr;
    const QSize buffer = surface ? surface->bufferSize() : QSize();
    const QSize output = window->output() ? window->output()->pixelSize() : QSize();
    if (buffer.isEmpty() || output.isEmpty()) {
        return;
    }
    if (!upscaleCoversOutput(effectWindow)) {
        // The window stopped covering its screen, which is the one outcome
        // that is visibly wrong rather than merely ineffective. Undo it at
        // once rather than spending the remaining patience on it.
        release(window);
        return;
    }
    if (buffer.width() < output.width() && buffer.height() < output.height()) {
        entry->answered = true;
        return;
    }
    if (++entry->frames >= patienceInFrames) {
        // The client read the hint and did nothing with it, which is what Qt
        // and SDL 2 do: neither honours a fractional scale. Give the scale
        // back so nothing carries a request the client is not acting on, and
        // let the status say no method reached it.
        release(window);
    }
}

void UpscaleWaylandScale::release(Window *window)
{
    const auto entry = m_requests.constFind(window);
    if (entry == m_requests.constEnd()) {
        return;
    }
    window->setNextTargetScale(entry->original);
    disconnect(window, nullptr, this, nullptr);
    m_requests.remove(window);
}

void UpscaleWaylandScale::releaseAll()
{
    const auto windows = m_requests.keys();
    for (Window *window : windows) {
        release(window);
    }
    m_requests.clear();
}

double UpscaleWaylandScale::requested(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry == m_requests.constEnd() ? 0 : entry->ratio;
}

bool UpscaleWaylandScale::answered(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry != m_requests.constEnd() && entry->answered;
}

} // namespace KWin
