/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <QHash>
#include <QObject>
#include <QRectF>
#include <QString>

namespace KWin
{

class EffectWindow;
class Window;

/**
 * Auto's Wayland half: asking one surface to render smaller, reversibly.
 *
 * The advertised screen mode reaches clients that size their buffer at
 * startup. This controller handles clients that instead follow a window's
 * scale or its configured size, after that window exists.
 *
 * The preferred fractional scale is sent per surface, after the window
 * exists, to one identified client, and setting it back restores what the
 * client had. That is what makes a ladder possible at all: ask, watch what the
 * client commits, and undo where it did not work.
 *
 * Auto falls back to a smaller fullscreen configure when the client ignores
 * the fractional scale. The effect presents that smaller window over its
 * output and maps pointer input; the original geometry is restored on release.
 * At desktop scale one, clients without fractional scaling get that size in
 * their first configure, before initializing a viewport that may stay fixed.
 *
 * Qt clamps the scale hint to one. SDL 2 acts on
 * it only for a window created high-DPI aware, and not in exclusive
 * fullscreen, where the buffer is the display mode it selected when the window
 * was made; that window needs the advertised mode, said when the client binds
 * its output. A client that ignores both live requests is not asked again
 * until the settings change.
 */
class UpscaleWaylandScale : public QObject
{
    Q_OBJECT

public:
    explicit UpscaleWaylandScale(QObject *parent = nullptr);
    ~UpscaleWaylandScale() override;

    /**
     * Ask @p window for @p ratio of its output, or give back what was asked.
     *
     * A ratio of one, or a window this cannot act on, releases it. Calling
     * this with the same ratio again costs nothing, which matters because the
     * caller is the frame path and asks on every candidate resolution.
     */
    void request(EffectWindow *window, double ratio, bool allowResize = false);

    /** The smaller fullscreen geometry Auto is holding, or an empty rectangle. */
    QRectF resizedGeometry(const Window *window) const;
    /** The buffer size requested through that geometry, in device pixels. */
    QSize requestedSize(const Window *window) const;

    /** Restore every window's scale and owned geometry on reconfiguration or teardown. */
    void releaseAll();

    /**
     * Give back what was asked of every window on @p output except @p kept.
     *
     * A window is asked only while it is the one its output would scale, and
     * one that stops being it - leaving fullscreen, losing its output to
     * another window - is no longer passed to request() at all, so this is
     * where its request ends. Left standing, it would even be re-asserted
     * whenever KWin set the window's scale back.
     */
    void releaseOthers(UpscaleOutput *output, const EffectWindow *kept);

    /** The ratio currently asked of @p window, or zero where none is. */
    double requested(const Window *window) const;

    /** Whether @p window has been asked anything since it last qualified. */
    bool known(const Window *window) const;

    /** Whether a request is still waiting for its client's answer. */
    bool asking() const;

    /**
     * Whether @p window answered the request: it committed a smaller buffer
     * and its picture still covers its output.
     *
     * Reported separately from the request, because a request is not a result
     * and the status must never present one as the other.
     */
    bool answered(const Window *window) const;
    /** What this keeps per window or program; see UpscaleEffect::records(). */
    QString records() const
    {
        return QStringLiteral("scaleRequests=%1").arg(m_requests.size());
    }

private:
    struct Request
    {
        double ratio = 1;
        double original = 1;
        /** Frames seen since the request, to decide it was ignored. */
        int frames = 0;
        bool answered = false;
        // The client drew at full size through its whole patience, and has
        // its own scale back. Kept rather than forgotten, so the same window
        // is not asked the same question again every thirty frames; a new
        // question - another ratio, or new settings - asks again.
        bool ignored = false;
        bool allowResize = false;
        bool resizing = false;
        QRectF geometry;
        QSize pixels;
    };

    static void apply(Window *window, const Request &request);
    void release(Window *window);
    void observe(Window *window);
    void checkAnswer(EffectWindow *effectWindow, Request &request);
    bool requestedPresentation(const Window *window) const;
    static bool resize(Window *window, Request &request);
    static void restoreGeometry(Window *window, const Request &request);
    void watchInitialSizes();
    void requestInitialSize(Window *window);

    // Keyed by the window itself. Window::closed removes the entry, so no key
    // here ever outlives what it points at.
    QHash<Window *, Request> m_requests;
};

} // namespace KWin
