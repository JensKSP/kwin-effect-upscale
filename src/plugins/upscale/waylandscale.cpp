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

#include <algorithm>
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
        if (!entry->ignored && std::abs(window->nextTargetScale() - wanted) > 0.001) {
            apply(window, *entry);
        }
    });
    connect(window, &Window::closed, this, [this, window]() {
        m_requests.remove(window);
    });
    // A window that stops being the one its output would scale - out of
    // fullscreen, off its output, minimized, moved - gets its scale back at
    // once. Nothing else would give it back: the frame path asks only the
    // window that qualifies, and with nothing qualifying the effect is not
    // even painting.
    const auto recheck = [this, window]() {
        EffectWindow *effectWindow = window->effectWindow();
        if (!effectWindow || upscaleWindowAwaitingBuffer(effectWindow->screen()) != effectWindow) {
            release(window);
        }
    };
    connect(window, &Window::fullScreenChanged, this, recheck);
    connect(window, &Window::frameGeometryChanged, this, recheck);
    connect(window, &Window::outputChanged, this, recheck);
    connect(window, &Window::minimizedChanged, this, recheck);
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
        entry->ignored = false;
        apply(window, *entry);
        return;
    }
    if (entry->answered || entry->ignored) {
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
        // does, and SDL 2 in exclusive fullscreen. Give the scale back so
        // nothing carries a request the client is not acting on, and let the
        // status say no method reached it.
        entry->ignored = true;
        window->setNextTargetScale(entry->original);
    }
}

bool UpscaleWaylandScale::known(const Window *window) const
{
    return m_requests.contains(const_cast<Window *>(window));
}

bool UpscaleWaylandScale::asking() const
{
    return std::ranges::any_of(m_requests, [](const Request &request) {
        return !request.answered && !request.ignored;
    });
}

void UpscaleWaylandScale::releaseOthers(UpscaleOutput *output, const EffectWindow *kept)
{
    const Window *keptWindow = kept ? kept->window() : nullptr;
    QList<Window *> released;
    for (auto entry = m_requests.cbegin(); entry != m_requests.cend(); ++entry) {
        Window *window = entry.key();
        if (window != keptWindow && window->effectWindow() && window->effectWindow()->screen() == output) {
            released.append(window);
        }
    }
    for (Window *window : std::as_const(released)) {
        release(window);
    }
}

void UpscaleWaylandScale::release(Window *window)
{
    const auto entry = m_requests.constFind(window);
    if (entry == m_requests.constEnd()) {
        return;
    }
    // Forgotten before the scale goes back, not after. Giving it back emits
    // nextTargetScaleChanged, and while a request stands that is the signal
    // that re-asserts it: in the other order every release was undone the
    // moment it was made.
    const double original = entry->original;
    disconnect(window, nullptr, this, nullptr);
    m_requests.remove(window);
    window->setNextTargetScale(original);
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
    return entry == m_requests.constEnd() || entry->ignored ? 0 : entry->ratio;
}

bool UpscaleWaylandScale::answered(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry != m_requests.constEnd() && entry->answered;
}

} // namespace KWin
