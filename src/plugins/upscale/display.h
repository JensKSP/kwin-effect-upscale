/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "framestatistics.h"
#include "overlay.h"
#include "placement.h"
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
 * It owns the visibility rules the handbook states, and the three separate
 * things they describe: a timed announcement of the selected window with a
 * basic summary, a persistent view of the measurements, and a developer dump.
 * Each is its own block in its own corner, because they are read for different
 * reasons and one of them disappears while the others stay. The effect owns
 * the rendering path and the state; this owns the decision of what a person
 * sees and where.
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
    // A new window needs its first paint; the same window stops requiring
    // composition when only its timed announcement was enabled and expires.
    bool activeFor(EffectWindow *window) const;

    /** One client buffer commit for the window being displayed. */
    void countClientUpdate(EffectWindow *window);

    /** One compositor repaint of the output being displayed. */
    void countRepaint();

    /**
     * Follow the frames this output actually presents.
     *
     * The measurements start again when the output changes, because frame
     * times from two screens are not one distribution.
     */
    void measure(UpscaleOutput *screen);

    /**
     * Whether the effect should build a fresh snapshot now. Building one costs
     * string formatting, so it happens at the sampling interval rather than
     * for every frame of a game running at full speed.
     */
    bool wantsSnapshot(EffectWindow *window) const;

    /**
     * Fill in the frames this screen presented.
     *
     * Separate from update(), because these measurements belong to the screen
     * rather than to the announcement: they are taken whether or not anything
     * is drawn, so a report asked for with the display switched off carries
     * them as well.
     */
    void reportPresentation(UpscaleSnapshot &snapshot) const;

    /** Takes a new snapshot, completes its measurements and lays out the text. */
    void update(UpscaleSnapshot snapshot, EffectWindow *window);

    /**
     * Copies the measurements as they stand into another snapshot.
     *
     * The frame times belong to this class because it is what follows one
     * window and one output long enough to have them. A caller reporting the
     * state somewhere other than the screen — the status the settings page
     * and D-Bus read — builds its own snapshot and has no way to measure, so
     * it asks for these rather than reporting a window without them.
     *
     * Nothing is copied while nothing is being measured, which leaves the
     * caller's snapshot saying so instead of quoting a stale rate.
     */
    void applyMeasurements(UpscaleSnapshot &snapshot, EffectWindow *window, UpscaleOutput *screen) const;

    /** Draws the text onto this output, whose logical geometry is given. */
    void paint(const RenderTarget &target, const RenderViewport &viewport, const UpscaleRectF &screen);

    /**
     * Hides everything and releases what it was holding.
     *
     * A display that had been drawn asks for one more frame on its way out.
     * KWin stops calling an inactive effect's paint hooks, and a screen with
     * nothing to repaint keeps the pixels it last composited, so without that
     * frame the blocks stay on the desktop after the game that they described
     * has gone, with later window repaints drawing over them.
     */
    void hide();

    /** Whether anything was drawn and is still on the screen. */
    bool drawn() const;

    /** Everything on screen right now, block by block, and empty when
     *  nothing is. The blocks are drawn apart; this reads them as one. */
    QString text() const;

private:
    void compose();
    void releaseBlocks();
    void resetSampling();

    // Three blocks in three places, each one the user's to choose and no two
    // of them the same. The defaults are where each block has always been.
    UpscaleOverlay m_announcementOverlay;
    UpscaleOverlay m_statisticsOverlay;
    UpscaleOverlay m_developerOverlay;
    UpscaleCorner m_announcementCorner = UpscaleCorner::TopLeft;
    UpscaleCorner m_statisticsCorner = UpscaleCorner::TopRight;
    UpscaleCorner m_developerCorner = UpscaleCorner::BottomRight;
    UpscaleSnapshot m_snapshot;
    bool m_enabled = true;
    bool m_detection = true;
    bool m_summary = true;
    bool m_statistics = false;
    bool m_developer = false;
    int m_timeout = 3;

    // The timed announcement belongs to a window, not its changing caption.
    // Repaints and title changes keep its deadline. A new selection or an
    // explicit reconfiguration starts a new announcement.
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
    // Frames as the screen presented them, which is neither what the client
    // committed nor what the compositor painted.
    UpscaleFrameStatistics m_presented;
    QPointer<UpscaleOutput> m_measured;
    QMetaObject::Connection m_presentation;
    int m_presentationMode = -1;
    QElapsedTimer m_composed;
    // Whether the last paint put anything on the screen, which decides
    // whether hiding has to ask for the frame that takes it off again.
    bool m_drawn = false;
};

} // namespace KWin
