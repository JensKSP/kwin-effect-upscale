/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "overlay.h"
#include "snapshot.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace KWin
{

class EffectWindow;

/**
 * What the on-screen display shows, and when.
 *
 * It owns the visibility rules the handbook states: a timed announcement of
 * the selected window with a basic summary, a persistent statistics view, and
 * developer information extending it. The effect owns the rendering path and
 * the state; this owns the decision of what a person sees.
 *
 * Nothing here samples on a timer of its own. Text is rebuilt at a bounded
 * interval while frames are being painted anyway, and the one timer that does
 * exist expires the timed announcement.
 */
class UpscaleDisplay : public QObject
{
    Q_OBJECT

public:
    UpscaleDisplay();
    ~UpscaleDisplay() override;

    /** Re-reads the display settings and forgets what was announced. */
    void reconfigure();

    /** Whether any mode is enabled at all. */
    bool enabled() const;

    /** One client buffer commit for the window being displayed. */
    void countClientUpdate();

    /** One compositor repaint of the output being displayed. */
    void countRepaint();

    /**
     * Whether the effect should build a fresh snapshot now. Building one costs
     * string formatting, so it happens at the sampling interval rather than
     * for every frame of a game running at full speed.
     */
    bool wantsSnapshot(EffectWindow *window) const;

    /** Takes a new snapshot, completes its measurements and lays out the text. */
    void update(UpscaleSnapshot snapshot, EffectWindow *window);

    /** Draws the text onto this output, whose logical geometry is given. */
    void paint(const RenderTarget &target, const RenderViewport &viewport, const UpscaleRectF &screen);

    /** Hides everything and releases what it was holding. */
    void hide();

    /** What is on screen right now, and empty when nothing is. */
    QString text() const;

private:
    void compose();

    UpscaleOverlay m_overlay;
    UpscaleSnapshot m_snapshot;
    bool m_enabled = true;
    bool m_detection = true;
    bool m_summary = true;
    bool m_statistics = false;
    bool m_developer = false;
    int m_timeout = 3;

    // The window and summary the timed announcement belongs to. Repaints,
    // title changes and focus returning to the same window must not announce
    // it again, so the announcement is keyed to what it actually said.
    QPointer<EffectWindow> m_announced;
    QString m_announcement;
    QElapsedTimer m_announcedAt;
    QTimer m_expiry;

    // Sampling. Counters accumulate until the interval is full, then become
    // the rates reported until the next one completes.
    QElapsedTimer m_sampled;
    int m_clientUpdates = 0;
    int m_repaints = 0;
    double m_clientUpdateRate = -1;
    double m_repaintRate = -1;
    double m_interval = 0;
    QElapsedTimer m_composed;
    // Where the text was last drawn, so that its own expiry can repaint it.
    UpscaleRectF m_area;
};

} // namespace KWin
