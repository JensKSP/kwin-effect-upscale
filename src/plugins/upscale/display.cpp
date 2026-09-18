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
        effects->addRepaintFull();
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
    m_timeout = UpscaleConfig::osdTimeout();
    // A changed setting is a new state to announce, not a continuation of the
    // old one, and the old text may describe a mode that is now switched off.
    m_announced.clear();
    m_announcement.clear();
    m_announcedAt.invalidate();
    m_overlay.release();
}

bool UpscaleDisplay::enabled() const
{
    return m_enabled && (m_detection || m_summary || m_statistics || m_developer);
}

void UpscaleDisplay::countClientUpdate()
{
    ++m_clientUpdates;
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

void UpscaleDisplay::update(UpscaleSnapshot snapshot, EffectWindow *window)
{
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
    snapshot.clientUpdates = m_clientUpdateRate;
    snapshot.repaints = m_repaintRate;
    snapshot.interval = m_interval;
    // Say how old the numbers are instead of implying they are current. A game
    // that stops supplying frames stops advancing this display as well.
    snapshot.sampleAge = m_interval > 0 ? double(m_sampled.elapsed()) / 1000 : 0;
    m_snapshot = snapshot;

    const QString announcement = upscaleAnnouncement(m_snapshot);
    if (window != m_announced || announcement != m_announcement) {
        m_announced = window;
        m_announcement = announcement;
        m_announcedAt.start();
        m_expiry.start(m_timeout * 1000);
    }
    m_composed.start();
    compose();
}

void UpscaleDisplay::compose()
{
    // Off means off, whoever asks. The caller stops painting a disabled
    // display, and a disabled display also has nothing to say if it is asked
    // anyway; one switch must not have two meanings.
    if (!enabled()) {
        m_overlay.setText(QString(), m_snapshot.outputScale);
        return;
    }
    QStringList blocks;
    const bool announcing = m_announcedAt.isValid() && m_announcedAt.elapsed() < qint64(m_timeout) * 1000;
    if (announcing && m_detection) {
        blocks.append(m_announcement);
    }
    if (announcing && m_summary) {
        blocks.append(upscaleBasicSummary(m_snapshot));
    }
    // Developer information extends the persistent view rather than replacing
    // it, so enabling it also shows the statistics it annotates.
    if (m_statistics || m_developer) {
        blocks.append(upscaleStatistics(m_snapshot));
    }
    if (m_developer) {
        blocks.append(upscaleDeveloperInformation(m_snapshot));
    }
    m_overlay.setText(blocks.join(QLatin1Char('\n')), m_snapshot.outputScale);
}

void UpscaleDisplay::paint(const RenderTarget &target, const RenderViewport &viewport, const UpscaleRectF &screen)
{
    // The timed part may have expired since the text was composed, and the
    // rest of it stays as it is. Recomposing is a string comparison when
    // nothing changed.
    compose();
    if (m_overlay.isEmpty()) {
        m_area = UpscaleRectF();
        return;
    }
    // The margin keeps the text off the edge of a television, which overscans.
    constexpr double margin = 32;
    const QPointF position = screen.topLeft() + QPointF(margin, margin);
    m_area = UpscaleRectF(position, m_overlay.size());
    m_overlay.paint(target, viewport, position);
}

QString UpscaleDisplay::text() const
{
    return m_overlay.text();
}

void UpscaleDisplay::hide()
{
    m_overlay.release();
    m_announced.clear();
    m_announcement.clear();
    m_announcedAt.invalidate();
    m_composed.invalidate();
    m_area = UpscaleRectF();
    m_expiry.stop();
}

} // namespace KWin
