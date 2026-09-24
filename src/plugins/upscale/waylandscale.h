/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <QHash>
#include <QObject>

namespace KWin
{

class EffectWindow;
class Window;

/**
 * Auto's Wayland half: asking one surface to render smaller, reversibly.
 *
 * The advertisement methods cannot serve Auto. They are made when the client
 * binds the output, before it has a window, so the presentation is not
 * knowable yet and the statement cannot be taken back once the client has read
 * it. A wrong one leaves a window that no longer covers the screen, and the
 * effect cannot resize a Wayland client.
 *
 * The preferred fractional scale can. It is sent per surface, after the window
 * exists, to one identified client, and setting it back restores what the
 * client had. That is what makes a ladder possible at all: ask, watch what the
 * client commits, and undo where it did not work.
 *
 * It is also the only lever that reaches an unscaled output. A wl_output scale
 * is a whole number with nothing below one, so on a television at 4K the
 * scale-based advertisements can say nothing; a fractional scale is a fraction
 * with no such floor.
 *
 * What it does not reach: a client that ignores the hint, or whose buffer
 * does not follow its surface's scale. Qt clamps the hint to one. SDL 2 acts on
 * it only for a window created high-DPI aware, and not in exclusive
 * fullscreen, where the buffer is the display mode it selected when the window
 * was made; that window needs the advertised mode, said when the client binds
 * its output. A client that did not answer is reported rather than asked
 * again, because it will not answer the same question the second time.
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
    void request(EffectWindow *window, double ratio);

    /** Give every window back its own scale, for reconfiguration and teardown. */
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
     * and its surface still covers its output.
     *
     * Reported separately from the request, because a request is not a result
     * and the status must never present one as the other.
     */
    bool answered(const Window *window) const;

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
    };

    static void apply(Window *window, const Request &request);
    void release(Window *window);
    void observe(Window *window);
    void checkAnswer(EffectWindow *effectWindow, Request &request);

    // Keyed by the window itself. Window::closed removes the entry, so no key
    // here ever outlives what it points at.
    QHash<Window *, Request> m_requests;
};

} // namespace KWin
