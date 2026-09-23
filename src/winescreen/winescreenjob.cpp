/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winescreenhelper.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <csignal>

Q_LOGGING_CATEGORY(KWIN_UPSCALE_WINESCREEN, "kwin.upscale.winescreen", QtInfoMsg)

void WineScreenHelper::afterRun(const WineScreenRecord &record, uint pid, pid_t server, const std::shared_ptr<WineDirectory> &directory,
                                const QList<WineScreen> &screens)
{
    if (hasJob(record.id)) {
        return;
    }
    startJob({
        .id = record.id,
        .offer = {},
        .game = static_cast<pid_t>(pid),
        .server = server,
        .clear = screens.isEmpty(),
        .screens = screens,
        .relaunch = false,
        .closeDeadline = {},
        .terminated = false,
        .directory = directory,
        .giveUp = std::nullopt,
        .settled = std::nullopt,
    });
}

bool WineScreenHelper::hasJob(const QString &id) const
{
    return std::ranges::any_of(m_jobs, [&id](const Job &job) {
        return job.id == id;
    });
}

QList<WineScreenRecord> WineScreenHelper::prepared() const
{
    QList<WineScreenRecord> records = m_records->all();
    records.removeIf([](const WineScreenRecord &record) {
        return !record.written;
    });
    return records;
}

bool WineScreenHelper::reset(const QString &id)
{
    const std::optional<WineScreenRecord> record = m_records->find(id);
    if (!record) {
        return false;
    }
    m_jobs.removeIf([&id](const Job &job) {
        return job.id == id;
    });
    if (!record->written) {
        m_records->remove(id);
        return true;
    }
    startJob({
        .id = id,
        .offer = {},
        .game = 0,
        .server = hostServer(*record).value_or(0),
        .clear = true,
        .screens = {},
        .relaunch = false,
        .closeDeadline = {},
        .terminated = false,
        .directory = nullptr,
        .giveUp = std::nullopt,
        .settled = std::nullopt,
    });
    return true;
}

bool WineScreenHelper::busy() const
{
    if (!m_jobs.isEmpty() || !m_probation.isEmpty()) {
        return true;
    }
    return std::ranges::any_of(m_offers, [](const Pending &pending) {
        return !pending.expiry.hasExpired();
    });
}

void WineScreenHelper::startJob(const Job &job)
{
    m_jobs.append(job);
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void WineScreenHelper::poll()
{
    for (qsizetype index = 0; index < m_jobs.size();) {
        if (advance(m_jobs[index])) {
            m_jobs.removeAt(index);
        } else {
            ++index;
        }
    }
    for (qsizetype index = 0; index < m_probation.size();) {
        if (watch(m_probation[index])) {
            m_probation.removeAt(index);
        } else {
            ++index;
        }
    }
    if (m_jobs.isEmpty() && m_probation.isEmpty()) {
        m_timer.stop();
    }
}

bool WineScreenHelper::watch(Probation &probation)
{
    const std::optional<WineScreenRecord> record = m_records->find(probation.id);
    if (!record || !record->written) {
        return true;
    }
    if (hostServer(*record)) {
        probation.started = true;
        return false;
    }
    if (!probation.started) {
        return probation.expiry.hasExpired();
    }
    qCWarning(KWIN_UPSCALE_WINESCREEN) << record->title << "ran and ended without drawing a window after its prefix was described for"
                                       << *record->written << "; taking that back and not offering it again";
    WineScreenRecord failed = *record;
    failed.never = true;
    m_records->store(failed);
    afterRun(failed, 0, 0, nullptr, {});
    return true;
}

void WineScreenHelper::proven(const QString &id)
{
    m_probation.removeIf([&id](const Probation &probation) {
        return probation.id == id;
    });
}

bool WineScreenHelper::stillRunning(Job &job)
{
    if (job.game > 0 && wineProcessExists(job.game)) {
        if (job.relaunch && !job.terminated && job.closeDeadline.hasExpired()) {
            ::kill(job.game, SIGTERM);
            job.terminated = true;
        }
        return true;
    }
    return job.server > 0 && wineProcessExists(job.server);
}

bool WineScreenHelper::advance(Job &job)
{
    if (stillRunning(job)) {
        job.settled.reset();
        return false;
    }
    if (!job.settled) {
        job.settled = QDeadlineTimer(settleDelay);
    }
    if (!job.settled->hasExpired()) {
        return false;
    }
    std::optional<WineScreenRecord> record = m_records->find(job.id);
    if (!record) {
        return true;
    }
    record->target.directory = job.directory;
    if (!job.clear) {
        const QString build = wineBuild(record->target.steamCompatData);
        if (!wineBuildTested(build)) {
            qCInfo(KWIN_UPSCALE_WINESCREEN) << "Describing the screen in a prefix of"
                                            << (build.isEmpty() ? QStringLiteral("a Wine build this companion cannot tell") : build)
                                            << ", which it was not tested against; a screen it does not read leaves the game at full size";
        }
    }
    WineWriteResult result = job.clear ? wineClearScreen(record->target, ::getuid())
                                       : wineSetScreens(record->target, ::getuid(), job.screens, QDateTime::currentSecsSinceEpoch());
    if (result == WineWriteResult::Written && !holdsWhatWasWritten(*record, job.clear, job.screens)) {
        qCWarning(KWIN_UPSCALE_WINESCREEN) << "The prefix of" << record->title << "did not keep what was written";
        result = WineWriteResult::WrittenMeanwhile;
    }
    if (result == WineWriteResult::WrittenMeanwhile) {
        record->written = job.clear ? std::nullopt : std::optional<QSize>(job.screens.value(0).rect.size());
        record->described = job.clear ? QString() : wineScreensText(job.screens);
        m_records->store(*record);
    }
    if (result == WineWriteResult::Busy || result == WineWriteResult::WrittenMeanwhile) {
        job.server = hostServer(*record).value_or(0);
        if (!job.giveUp) {
            job.giveUp = QDeadlineTimer(busyLimit);
        }
        if (!job.giveUp->hasExpired()) {
            return false;
        }
        qCWarning(KWIN_UPSCALE_WINESCREEN) << "Gave up waiting for the prefix of" << record->title;
    }
    settle(job, *record, result);
    Q_EMIT jobFinished(job.id, result);
    return true;
}

void WineScreenHelper::settle(const Job &job, WineScreenRecord &record, WineWriteResult result)
{
    const QList<WineScreen> described = record.target.directory ? wineScreensIn(*record.target.directory) : QList<WineScreen>{};
    qCInfo(KWIN_UPSCALE_WINESCREEN) << (job.clear ? "Undo of" : "Preparation of") << record.title << wineScreensText(job.screens) << "result"
                                    << static_cast<int>(result) << "prefix now describes" << wineScreensText(described);
    if (result == WineWriteResult::Written && job.clear) {
        record.written.reset();
        record.never ? m_records->store(record) : m_records->remove(job.id);
        return;
    }
    if (result == WineWriteResult::Written) {
        record.written = job.screens.value(0).rect.size();
        record.described = wineScreensText(job.screens);
        m_records->store(record);
        if (job.relaunch && !m_launcher(record)) {
            qCWarning(KWIN_UPSCALE_WINESCREEN) << "Could not start" << record.title << "again";
        } else if (job.relaunch) {
            m_probation.append({.id = job.id, .started = false, .expiry = QDeadlineTimer(watchLimit)});
            m_timer.start();
        }
        return;
    }
    qCWarning(KWIN_UPSCALE_WINESCREEN) << "Left the prefix of" << record.title << "unchanged, result" << static_cast<int>(result);
    if (result == WineWriteResult::DesktopOfTheUser && !job.clear) {
        record.written.reset();
        m_records->store(record);
    }
}
