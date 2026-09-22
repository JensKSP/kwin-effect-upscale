/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "compatibility.h"
#include "x11geometry.h"

#include "x11input.h"

#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "x11window.h"

#include <KLocalizedString>
#endif

namespace KWin
{

#if KWIN_BUILD_X11

QString UpscaleX11Resolution::unmetCondition(const Request &request)
{
    // Four separate conditions decide whether a request was honoured, and the
    // person reading the answer has to act on the one that actually failed.
    // Reporting them as one sentence about the application was measurably
    // wrong: on 2026-09-19 Left 4 Dead 2 supplied exactly the requested
    // 2560 x 1440 buffer and was still told it had not, because what was
    // missing was the emulated mode its toolkit never asks for. That mode is
    // no longer required - the effect presents such a window itself - but a
    // buffer of the wrong size and a frame that left the output still lead
    // somewhere different from each other, so they still arrive apart.
    X11Window *window = request.window;
    if (!window->output()) {
        return i18n("The window is not on an output.");
    }
    SurfaceItem *surface = window->effectWindow()->windowItem()->surfaceItem();
    if (!surface) {
        return i18n("The window has no surface to read a buffer from.");
    }
    const QSize supplied = surface->bufferSize();
    if (supplied != request.size) {
        return i18n("The application supplied a %1 x %2 buffer where %3 x %4 was requested.",
                    supplied.width(), supplied.height(), request.size.width(), request.size.height());
    }
    // KWin's frame is what the buffer is presented across, by Xwayland or by
    // this effect, so it has to have stayed on the output either way.
    const QRectF frame = window->frameGeometry();
    const QRectF output = window->output()->geometryF();
    if (frame != output) {
        return i18n("The window stopped covering its output: it is %1 x %2 at %3, %4 where the output is "
                    "%5 x %6 at %7, %8.",
                    frame.width(), frame.height(), frame.x(), frame.y(),
                    output.width(), output.height(), output.x(), output.y());
    }
    // Either Xwayland's viewport or present() has made the surface the
    // frame's size by now; which one is reported in status, not decided here.
    // A surface at any other size is the one thing the scaler cannot stand
    // in for, because KWin clips the window's paint to it.
    const QSizeF destination = surface->destinationSize();
    if (destination != frame.size()) {
        return i18n("The supplied buffer is presented at %1 x %2 rather than at the window's %3 x %4.",
                    destination.width(), destination.height(), frame.width(), frame.height());
    }
    return {};
}

void UpscaleX11Resolution::validate(const QString &key, int generation, int revision)
{
    if (generation != m_generation || revision != m_validation.value(key) || m_failures.contains(key)) {
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
            if (retry(key, generation)) {
                return;
            }
            refuse(key, unmet);
            return;
        }
        observed = true;
    }
    if (observed) {
        m_attempts.remove(key);
        // The surface reached its requested size somewhere in the last three
        // seconds; a pointer that has not moved since still has to follow it.
        m_input->refresh();
    }
}

#endif

} // namespace KWin
