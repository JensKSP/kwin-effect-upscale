/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "display.h"

#include "upscaleconfig.h"

#include "effect/effecthandler.h"

namespace KWin
{

UpscaleDisplay::UpscaleDisplay()
{
    m_expiry.setSingleShot(true);
    // The timed announcement disappears on its own. Nothing else would repaint
    // the area if the game stopped drawing, so ask for that one repaint here.
    connect(&m_expiry, &QTimer::timeout, this, []() {
        // Only a loaded effect has a compositor to ask for a repaint. The
        // display is also driven directly by tests, where there is none.
        if (effects) {
            effects->addRepaintFull();
        }
    });
}

UpscaleDisplay::~UpscaleDisplay() = default;

void UpscaleDisplay::reconfigure()
{
    m_enabled = UpscaleConfig::osd();
    m_detection = UpscaleConfig::osdDetection();
    m_summary = UpscaleConfig::osdSummary();
    m_statistics = UpscaleConfig::osdStatistics();
    m_developer = UpscaleConfig::osdDeveloper();
    m_statisticsCorner = upscaleCorner(UpscaleConfig::osdPosition());
    m_timeout = UpscaleConfig::osdTimeout();
    // New settings invalidate both the announcement and its measurements.
    hide();
}

bool UpscaleDisplay::enabled() const
{
    return m_enabled && (m_detection || m_summary || m_statistics || m_developer);
}

bool UpscaleDisplay::activeFor(EffectWindow *window) const
{
    return enabled() && (m_statistics || m_developer || window != m_announced || !m_announcedAt.isValid() || m_announcedAt.elapsed() < qint64(m_timeout) * 1000);
}

void UpscaleDisplay::countClientUpdate(EffectWindow *window)
{
    // Count only the window whose sample is running, including a refused
    // fullscreen window. Other clients' commits are not this window's rate.
    if (window == m_announced && m_sampled.isValid()) {
        ++m_clientUpdates;
    }
}

void UpscaleDisplay::countRepaint()
{
    ++m_repaints;
}

bool UpscaleDisplay::wantsSnapshot(EffectWindow *window) const
{
    // Deliberately slow: this is a state display, not an animation, and a game
    // running at full speed must not pay for formatting it on every frame.
    constexpr qint64 composeInterval = 500;
    return window != m_announced || !m_composed.isValid() || m_composed.elapsed() >= composeInterval;
}

void UpscaleDisplay::measure(UpscaleOutput *screen)
{
    if (m_measured == screen) {
        return;
    }
    // Frame times from two screens are not one distribution, and a screen
    // that was unplugged has nothing left to say about the next one.
    disconnect(m_presentation);
    m_presented.reset();
    m_presentationMode = -1;
    m_measured = screen;
    RenderLoop *loop = upscaleRenderLoop(screen);
    if (!loop) {
        return;
    }
    m_presentation = connect(loop, &RenderLoop::framePresented, this,
                             [this](RenderLoop *, std::chrono::nanoseconds timestamp, PresentationMode mode) {
        // The moment the screen showed it, not the moment anything drew it.
        m_presented.record(double(timestamp.count()) / 1000000.0);
        m_presentationMode = int(mode);
    });
}

void UpscaleDisplay::update(UpscaleSnapshot snapshot, EffectWindow *window)
{
    if (window != m_announced) {
        resetSampling();
    }
    // How long counting runs before it becomes a reported rate. The interval
    // is part of what the display says, so that a rate is never presented
    // without the window it was measured over.
    constexpr qint64 sampleInterval = 1000;
    if (!m_sampled.isValid()) {
        m_sampled.start();
    } else if (m_sampled.elapsed() >= sampleInterval) {
        const double seconds = double(m_sampled.restart()) / 1000;
        m_clientUpdateRate = m_clientUpdates / seconds;
        m_repaintRate = m_repaints / seconds;
        m_interval = seconds;
        m_clientUpdates = 0;
        m_repaints = 0;
    }
    snapshot.presentedRate = m_presented.averageRate();
    snapshot.presentedLow = m_presented.lowRate(0.01);
    snapshot.presentedPercentile = m_presented.percentileFrameTime(0.99);
    snapshot.presentedWorst = m_presented.worstFrameTime();
    snapshot.presentedFrames = int(m_presented.frames());
    snapshot.presentation = m_presentationMode;
    snapshot.clientUpdates = m_clientUpdateRate;
    snapshot.repaints = m_repaintRate;
    snapshot.interval = m_interval;
    // Say how old the numbers are instead of implying they are current. A game
    // that stops supplying frames stops advancing this display as well.
    snapshot.sampleAge = m_interval > 0 ? double(m_sampled.elapsed()) / 1000 : 0;
    m_snapshot = snapshot;

    m_announcement = upscaleAnnouncement(m_snapshot);
    if (window != m_announced || !m_announcedAt.isValid()) {
        m_announced = window;
        m_announcedAt.start();
        m_expiry.start(m_timeout * 1000);
    }
    m_composed.start();
    compose();
}

void UpscaleDisplay::compose()
{
    if (!enabled() || !m_composed.isValid()) {
        releaseBlocks();
        return;
    }
    QStringList timed;
    const bool announcing = m_announcedAt.isValid() && m_announcedAt.elapsed() < qint64(m_timeout) * 1000;
    if (announcing && m_detection) {
        timed.append(m_announcement);
    }
    if (announcing && m_summary) {
        timed.append(upscaleBasicSummary(m_snapshot));
    }
    // Three texts, laid out separately. A block a person did not ask for is
    // given no text at all, which is also what releases what it was holding.
    // Turning the developer dump on no longer changes the view beside it: the
    // measurements are their own block, in their own place, either way.
    m_announcementOverlay.setText(timed.join(QLatin1Char('\n')), m_snapshot.outputScale);
    // Larger than the rest: this is the block read at a glance mid-game, from
    // as far away as the player is sitting, and it holds five figures for it.
    constexpr double headsUpEmphasis = 1.6;
    m_statisticsOverlay.setText(m_statistics ? upscaleHeadsUp(m_snapshot) : QString(), m_snapshot.outputScale, headsUpEmphasis);
    m_developerOverlay.setText(m_developer ? upscaleDeveloperInformation(m_snapshot) : QString(), m_snapshot.outputScale);
}

void UpscaleDisplay::releaseBlocks()
{
    m_announcementOverlay.release();
    m_statisticsOverlay.release();
    m_developerOverlay.release();
}

void UpscaleDisplay::applyMeasurements(UpscaleSnapshot &snapshot) const
{
    // The frames come from the running statistics rather than from the last
    // snapshot this display composed. The screen is followed whether or not
    // anything is drawn on it, so a report asked for while the display is
    // switched off still has frames to give; reading the composed snapshot
    // would answer "nothing has been presented" for a screen that is plainly
    // presenting. No frames at all is the one case worth testing, because the
    // rest are only meaningful once intervals have been counted.
    if (m_presented.frames() > 0) {
        snapshot.presentedRate = m_presented.averageRate();
        snapshot.presentedLow = m_presented.lowRate(0.01);
        snapshot.presentedPercentile = m_presented.percentileFrameTime(0.99);
        snapshot.presentedWorst = m_presented.worstFrameTime();
        snapshot.presentedFrames = int(m_presented.frames());
        snapshot.presentation = m_presentationMode;
    }
    // The counted rates are the display's own sampling, which only runs while
    // it is following a window, so they stay as the last completed sample.
    if (m_clientUpdateRate >= 0) {
        snapshot.clientUpdates = m_clientUpdateRate;
        snapshot.repaints = m_repaintRate;
        snapshot.interval = m_interval;
        snapshot.sampleAge = m_snapshot.sampleAge;
    }
}

void UpscaleDisplay::paint(const RenderTarget &target, const RenderViewport &viewport, const UpscaleRectF &screen)
{
    // The timed part may have expired since the text was composed, and the
    // rest of it stays as it is. Recomposing is a string comparison when
    // nothing changed.
    compose();
    // The margin keeps the text off the edge of a television, which overscans.
    constexpr double margin = 32;
    const auto size = screen.size();
    UpscaleCornerLayout layout(screen.topLeft(), QSizeF(size.width(), size.height()), margin);
    m_drawn = false;
    const auto draw = [&](UpscaleOverlay &block, UpscaleCorner corner) {
        if (block.isEmpty()) {
            return;
        }
        m_drawn = block.paint(target, viewport, layout.place(corner, block.size())) || m_drawn;
    };
    // The persistent view is placed first, so that it keeps the corner the
    // user chose even when the developer dump was sent to the same one.
    draw(m_statisticsOverlay, m_statisticsCorner);
    // A message arrives where messages arrive, and the dump stays out of the
    // way at the bottom. Neither corner is a setting: only the view a person
    // keeps on screen while playing is worth moving.
    draw(m_developerOverlay, UpscaleCorner::BottomRight);
    draw(m_announcementOverlay, UpscaleCorner::TopLeft);
}

QString UpscaleDisplay::text() const
{
    QStringList blocks;
    for (const UpscaleOverlay *block : {&m_announcementOverlay, &m_statisticsOverlay, &m_developerOverlay}) {
        if (!block->isEmpty()) {
            blocks.append(block->text());
        }
    }
    return blocks.join(QLatin1Char('\n'));
}

bool UpscaleDisplay::drawn() const
{
    return m_drawn;
}

void UpscaleDisplay::hide()
{
    const bool wasDrawn = m_drawn;
    m_drawn = false;
    releaseBlocks();
    m_announced.clear();
    m_announcement.clear();
    m_announcedAt.invalidate();
    m_composed.invalidate();
    m_expiry.stop();
    resetSampling();
    // Only a loaded effect has a compositor to ask, and only a display that
    // reached the screen leaves anything behind to erase.
    if (wasDrawn && effects) {
        effects->addRepaintFull();
    }
}

void UpscaleDisplay::resetSampling()
{
    // Only what the announcement itself counted. The frames the screen
    // presented belong to the screen, and measure() ends their life when it
    // changes. Clearing them here would discard the measurement on every
    // frame composed with the display switched off, because that path hides
    // the display, and nothing would ever be left to report.
    m_sampled.invalidate();
    m_clientUpdates = 0;
    m_repaints = 0;
    m_clientUpdateRate = -1;
    m_repaintRate = -1;
    m_interval = 0;
}

} // namespace KWin
