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

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

// How many frames a client is given to act on the hint before it is taken
// back. A client that honours it resizes on the configure that carries the
// scale, so this is generous rather than finely judged; what it must not be
// is unbounded, because a client that never answers would otherwise keep a
// scale it is not using and the status would keep claiming a pending request.
static constexpr int patienceInFrames = 30;

// Whether what KWin has already asked of the window still presents it over its
// whole output. The committed state lags that by a configure: a window KWin is
// restoring, or giving its decoration back, still covers its output until the
// client answers, and a scale asked for or kept meanwhile reaches the client
// in a configure of its own, with a decoration built for the wrong scale. A
// window leaving fullscreen for a maximized, decorated state still covers its
// output, but the decoration KWin has scheduled means it presents nothing
// borderless.
bool UpscaleWaylandScale::requestedPresentation(const Window *window) const
{
    const QRectF resized = resizedGeometry(window);
    if (!resized.isEmpty() && window->isRequestedFullScreen() && window->moveResizeGeometry() == resized) {
        return true;
    }
    return upscaleRequestCoversOutput(window) && (window->isFullScreen() || !window->nextDecoration());
}

UpscaleWaylandScale::UpscaleWaylandScale(QObject *parent)
    : QObject(parent)
{
    watchInitialSizes();
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
    qCInfo(KWIN_UPSCALE) << "Wayland scale request: window" << window->internalId() << "pid" << window->pid()
                         << "previous" << window->nextTargetScale() << "target" << request.original * request.ratio;
    window->setNextTargetScale(request.original * request.ratio);
}

void UpscaleWaylandScale::observe(Window *window)
{
    connect(window, &Window::nextTargetScaleChanged, this, [this, window]() {
        const auto entry = m_requests.find(window);
        if (entry == m_requests.end()) {
            return;
        }
        // Either the value this effect set, heard back, or KWin's: the scale
        // of the window's output, set again because the window moved or the
        // output's scale changed. KWin's value is the window's own scale from
        // then on, the one a ratio is asked of and the one given back.
        const double current = window->nextTargetScale();
        const double set = entry->ignored || entry->resizing ? entry->original : entry->original * entry->ratio;
        if (std::abs(current - set) <= 0.001) {
            return;
        }
        entry->original = current;
        if (entry->resizing) {
            release(window);
        } else if (!entry->ignored) {
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
        // The first configure precedes both the first buffer and the effect
        // window. Its request is judged once that window is mapped.
        if (!window->readyForPainting()) {
            return;
        }
        EffectWindow *effectWindow = window->effectWindow();
        if (!effectWindow || upscaleWindowAwaitingBuffer(effectWindow->screen()) != effectWindow
            || !requestedPresentation(window)) {
            release(window);
        }
    };
    // Asked as soon as KWin requests a new geometry or state, before the
    // configure that carries it is sent, so that the scale goes back in that
    // same configure rather than one after it, and a decoration built for it
    // is built at the scale restored.
    connect(window, &Window::frameGeometryAboutToChange, this, recheck);
    connect(window, &Window::fullScreenChanged, this, recheck);
    connect(window, &Window::frameGeometryChanged, this, recheck);
    connect(window, &Window::outputChanged, this, recheck);
    connect(window, &Window::minimizedChanged, this, recheck);
}

void UpscaleWaylandScale::request(EffectWindow *effectWindow, double ratio, bool allowResize)
{
    Window *window = effectWindow ? effectWindow->window() : nullptr;
    if (!window || !effectWindow->isWaylandClient()) {
        return;
    }
    if (ratio >= 1.0 || ratio <= 0.0 || !requestedPresentation(window)) {
        release(window);
        return;
    }
    auto entry = m_requests.find(window);
    if (entry == m_requests.end()) {
        Request fresh;
        // The scale the window had before anything was asked of it, which is
        // what it gets back. KWin setting it again while the request stands
        // replaces it (observe()).
        fresh.original = window->nextTargetScale();
        fresh.ratio = ratio;
        fresh.allowResize = allowResize;
        entry = m_requests.insert(window, fresh);
        observe(window);
        apply(window, *entry);
        return;
    }
    if (std::abs(entry->ratio - ratio) > 0.001) {
        if (entry->resizing) {
            release(window);
            return;
        }
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

    checkAnswer(effectWindow, *entry);
}

void UpscaleWaylandScale::checkAnswer(EffectWindow *effectWindow, Request &request)
{
    Window *window = effectWindow->window();
    const QSize buffer = upscaleSuppliedSize(upscalePictureSurface(effectWindow));
    const QSize output = window->output() ? window->output()->pixelSize() : QSize();
    if (buffer.isEmpty() || output.isEmpty()) {
        return;
    }
    if (!upscaleCoversOutput(effectWindow) && !upscaleDrawnOverOutput(effectWindow)) {
        // The window stopped covering its screen, which is the one outcome
        // that is visibly wrong rather than merely ineffective. Undo it at
        // once rather than spending the remaining patience on it.
        release(window);
        return;
    }
    if (buffer.width() < output.width() && buffer.height() < output.height()) {
        qCInfo(KWIN_UPSCALE) << "Wayland scale answered: window" << window->internalId() << "buffer" << buffer << "output" << output;
        request.answered = true;
        return;
    }
    if (++request.frames >= patienceInFrames) {
        if (!request.resizing && request.allowResize && resize(window, request)) {
            return;
        }
        // The client read the hint and did nothing with it, which is what Qt
        // does, and SDL 2 in exclusive fullscreen. Give the scale back so
        // nothing carries a request the client is not acting on, and let the
        // status say no method reached it.
        request.ignored = true;
        qCInfo(KWIN_UPSCALE) << "Wayland scale ignored: window" << window->internalId() << "buffer" << buffer
                             << "restoring scale" << request.original;
        window->setNextTargetScale(request.original);
        restoreGeometry(window, request);
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
    const Request saved = *entry;
    const double original = saved.original;
    disconnect(window, nullptr, this, nullptr);
    m_requests.remove(window);
    qCInfo(KWIN_UPSCALE) << "Wayland scale restored: window" << window->internalId() << "scale" << original;
    window->setNextTargetScale(original);
    restoreGeometry(window, saved);
    // A window leaving fullscreen gets its decoration back before it stops
    // qualifying, so KWin built that decoration, and the borders its next
    // configure subtracts, at the scale this effect asked for. Built again at
    // the scale restored here, the client is sent the size it had before:
    // measured 2026-09-27, KWin 6.3.6's own tests saw 498x250 for 500x250.
    if (!window->isDeleted()) {
        window->invalidateDecoration();
    }
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
    return entry == m_requests.constEnd() || entry->ignored || entry->resizing ? 0 : entry->ratio;
}

bool UpscaleWaylandScale::answered(const Window *window) const
{
    const auto entry = m_requests.constFind(const_cast<Window *>(window));
    return entry != m_requests.constEnd() && entry->answered;
}

} // namespace KWin
