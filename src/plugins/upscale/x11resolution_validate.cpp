/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "compatibility.h"
#include "runtime.h"
#include "windowidentity.h"
#include "x11geometry.h"

#include "x11input.h"

#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "x11window.h"

#include <KLocalizedString>
#include <QLoggingCategory>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)
#endif

namespace KWin
{

#if KWIN_BUILD_X11

QString UpscaleX11Resolution::unmetCondition(const Request &request)
{
    // A smaller drawable alone cannot prove that a client handled its resize.
    // A profile may require explicit mode selection as confirmation. Other
    // profiles allow the effect to present the resized buffer itself. Report
    // the particular condition that failed.
    X11Window *window = request.window;
    if (!window->output()) {
        return i18nc("A reason, after “request failed:” or “failure”", "the window is not on an output");
    }
    SurfaceItem *surface = window->effectWindow()->windowItem()->surfaceItem();
    if (!surface) {
        return i18nc("A reason, after “request failed:” or “failure”", "the window has no surface to read a buffer from");
    }
    const QSize supplied = surface->bufferSize();
    if (supplied != request.size) {
        return i18nc("A reason, after “request failed:” or “failure”", "the application supplied a %1 × %2 buffer where %3 × %4 was requested",
                     supplied.width(), supplied.height(), request.size.width(), request.size.height());
    }
    // A game resized after it started confirms through the mode it sets that
    // it took the size, as its entry can ask. One the proxy told the smaller
    // screen started at it and sets no mode: Extreme Tux Racer waited for one
    // forever (KWin 6.6.6, 2026-10-07).
    const UpscaleApplication *application = upscaleApplicationForWindow(window);
    if (application && application->x11RequiresEmulatedMode && !upscaleServed(window->pid())
        && !upscaleX11ModeMatches(window, request.position, request.size)) {
        return i18nc("A reason, after “request failed:” or “failure”", "the application has not confirmed the requested resolution through its X11 mode");
    }
    // KWin's frame is what the buffer is presented across, by Xwayland or by
    // this effect, so it has to have stayed on the output either way.
    const QRectF frame = window->frameGeometry();
    const QRectF output = window->output()->geometryF();
    if (frame != output) {
        return i18nc("A reason, after “request failed:” or “failure”", "the window stopped covering its output: it is %1 × %2 at %3, %4 where the output is "
                                                                       "%5 × %6 at %7, %8",
                     frame.width(), frame.height(), frame.x(), frame.y(),
                     output.width(), output.height(), output.x(), output.y());
    }
    // Either Xwayland's viewport or present() has made the surface the
    // frame's size by now; which one is reported in status, not decided here.
    // A surface at any other size is the one thing the scaler cannot stand
    // in for, because KWin clips the window's paint to it.
    const QSizeF destination = surface->destinationSize();
    if (destination != frame.size()) {
        return i18nc("A reason, after “request failed:” or “failure”", "the supplied buffer is presented at %1 × %2 rather than at the window's %3 × %4",
                     destination.width(), destination.height(), frame.width(), frame.height());
    }
    return {};
}

void UpscaleX11Resolution::validate(const QString &key, int generation, int revision)
{
    const auto negotiation = m_negotiations.constFind(key);
    if (generation != m_generation || negotiation == m_negotiations.cend()
        || revision != negotiation->validation || negotiation->failure.has_value()) {
        return;
    }
    bool observed = false;
    for (Request &request : m_requests) {
        if (request.key != key || !request.window || request.window->isDeleted()) {
            continue;
        }
        // Whatever the client did with its time, it is not waited for any
        // longer: a release of this request no longer needs its answer.
        request.answered = true;
        const QString unmet = unmetCondition(request);
        if (!unmet.isEmpty()) {
            ++m_judgements;
            qCInfo(KWIN_UPSCALE) << "X11 validation unmet:" << key << "window" << request.window->window() << unmet;
            if (retry(key, generation)) {
                return;
            }
            refuse(key, unmet);
            return;
        }
        qCInfo(KWIN_UPSCALE) << "X11 request accepted:" << key << "window" << request.window->window()
                             << "buffer" << request.size << "presented by" << (request.presentedByEffect ? "effect" : "Xwayland");
        observed = true;
    }
    if (!observed) {
        return;
    }
    ++m_judgements;
    const auto accepted = m_negotiations.find(key);
    if (accepted != m_negotiations.end()) {
        accepted->attempt = {};
    }
    // The surface reached its requested size somewhere in the last three
    // seconds; a pointer that has not moved since still has to follow it.
    m_input->refresh();
}

bool UpscaleX11Resolution::retry(const QString &key, int generation)
{
    Negotiation &negotiation = m_negotiations[key];
    if (negotiation.retried) {
        return false;
    }
    // Clients can discard resize events during a loading/state transition.
    // One retry returns to normal geometry first: duplicate ConfigureNotify
    // events may be ignored if the toolkit cached the requested size already.
    // Never loop on a client which cannot establish full-output presentation.
    qCInfo(KWIN_UPSCALE) << "X11 retry after restoring normal geometry:" << key;
    negotiation.retried = true;
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        if (m_requests.value(window).key != key) {
            continue;
        }
        const QPointer<X11Window> guarded = window;
        restore(window);
        QTimer::singleShot(250, this, [this, guarded, generation]() {
            if (guarded && generation == m_generation) {
                schedule(guarded);
            }
        });
    }
    return true;
}

void UpscaleX11Resolution::refuse(const QString &key, const QString &reason)
{
    Negotiation &negotiation = m_negotiations[key];
    negotiation.failure = reason;
    negotiation.requested = {};
    qCWarning(KWIN_UPSCALE) << "X11 resolution control:" << key << reason;
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        if (m_requests.value(window).key == key) {
            restore(window);
        }
    }
}

#endif

} // namespace KWin
