/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktophelper.h"
#include "wineprefix.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QDir>
#include <QLoggingCategory>
#include <QProcess>
#include <QUuid>

#include <algorithm>
#include <csignal>
#include <unistd.h>

Q_LOGGING_CATEGORY(KWIN_UPSCALE_WINEDESKTOP, "kwin.upscale.winedesktop", QtInfoMsg)

namespace
{

const QString acceptAnswer = QStringLiteral("accept");
const QString neverAnswer = QStringLiteral("never");

// Wine keeps its servers' locks below the literal /tmp, not $TMPDIR
// (server/request.c, create_server_dir).
const QString hostTemporaryDirectory = QStringLiteral("/tmp");

// An offer nobody answered does not keep the service alive for ever.
constexpr std::chrono::hours offerLifetime{1};

bool launchThroughSteam(const WineDesktopRecord &record)
{
    if (record.steamAppId.isEmpty()) {
        return false;
    }
    // xdg-open reaches whichever Steam handles steam:// links, native or Flatpak.
    return QProcess::startDetached(QStringLiteral("xdg-open"), {QStringLiteral("steam://rungameid/%1").arg(record.steamAppId)});
}

QString sizeText(const QSize &size)
{
    // Not through i18n's number formatting: a resolution has no thousands
    // separator.
    return i18nc("@item a resolution, width by height", "%1 × %2", QString::number(size.width()), QString::number(size.height()));
}

QString question(const QString &title, const QSize &size)
{
    return i18nc("@info the question shown in the middle of the screen; %1 is the game's window title, %2 a resolution",
                 "%1 ignores the resolution it is asked for and renders at the full size of the screen.\n"
                 "It can be set up to render at %2 inside a Wine desktop that is enlarged to the screen.\n"
                 "This is stored in the game's Wine prefix and takes effect from its next start.\n"
                 "It stays there if the upscaler is uninstalled, until it is reset in the upscaler's settings.",
                 title,
                 sizeText(size));
}

QString restartQuestion(const QString &title)
{
    return i18nc("@info the offer shown in the middle of the screen; %1 is the game's window title",
                 "Restart %1 now so that the change takes effect?\n"
                 "Progress that has not been saved may be lost.",
                 title);
}

WineDesktopTarget targetFor(const WinePrefix &prefix, const QProcessEnvironment &environment)
{
    // The paths the game used; the write checks that they still lead to the
    // directory proven now.
    return {
        .prefix = prefix.gamePath,
        .identity = prefix.identity,
        .steamCompatData = prefix.steamAppId.isEmpty() ? QString() : QDir::cleanPath(environment.value(QStringLiteral("STEAM_COMPAT_DATA_PATH"))),
        .temporaryDirectory = hostTemporaryDirectory,
        .directory = nullptr,
    };
}

std::optional<pid_t> hostServer(const WineDesktopRecord &record)
{
    return wineServerProcess(wineServerLockPath(record.target.temporaryDirectory, ::getuid(), record.target.identity));
}

} // namespace

WineDesktopHelper::WineDesktopHelper(WineDesktopRecords *records, QObject *parent)
    : QObject(parent)
    , m_records(records)
    , m_launcher(launchThroughSteam)
{
    m_timer.setInterval(std::chrono::seconds(1));
    connect(&m_timer, &QTimer::timeout, this, &WineDesktopHelper::poll);
}

void WineDesktopHelper::setLauncher(const Launcher &launcher)
{
    m_launcher = launcher;
}

void WineDesktopHelper::setTiming(std::chrono::milliseconds poll, std::chrono::milliseconds closeGrace)
{
    m_timer.setInterval(poll);
    m_closeGrace = closeGrace;
}

WineDesktopHelper::Offered WineDesktopHelper::offer(uint pid, const QString &windowClass, const QString &title, const QSize &size)
{
    const std::optional<WineProcess> process = size.isEmpty() ? std::nullopt : wineProcess(pid);
    if (!process) {
        return {};
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "No provable Wine prefix for process" << pid << "reason" << static_cast<int>(prefix.error());
        return {};
    }
    // Waiting for this process is what makes the later write safe. A server
    // in a process namespace this helper cannot see has no number here, and
    // then nothing is offered.
    const std::optional<pid_t> server = wineServerProcess(wineServerLockPath(prefix->temporaryDirectory, ::getuid(), prefix->identity));
    if (!server) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "The Wine server of process" << pid << "is out of sight";
        return {};
    }
    const QString id = wineRecordId(prefix->identity);
    WineDesktopRecord record = m_records->find(id).value_or(WineDesktopRecord{});
    if (record.never || record.written == size) {
        return {};
    }
    // Held from now on: after the game has gone, it still reaches the
    // directory proven now, whatever paths the host has.
    const std::shared_ptr<WineDirectory> directory = WineDirectory::open(prefix->path, prefix->identity);
    if (!directory) {
        return {};
    }
    record.id = id;
    record.title = title;
    record.target = targetFor(*prefix, process->environment);
    record.target.directory = directory;
    record.steamAppId = prefix->steamAppId;
    record.wanted = size;
    const QString offer = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_offers.insert(offer, Pending{.record = record, .game = static_cast<pid_t>(pid), .server = *server, .expiry = QDeadlineTimer(offerLifetime)});
    return {.offer = offer, .question = question(title, size)};
}

QString WineDesktopHelper::answer(const QString &offer, const QString &answer)
{
    const Pending pending = m_offers.take(offer);
    if (pending.record.id.isEmpty() || pending.expiry.hasExpired()) {
        return {};
    }
    if (answer == neverAnswer) {
        WineDesktopRecord record = pending.record;
        record.never = true;
        m_records->store(record);
        return {};
    }
    if (answer != acceptAnswer) {
        return {};
    }
    m_records->store(pending.record);
    startJob({
        .id = pending.record.id,
        .offer = offer,
        .game = pending.game,
        .server = pending.server,
        .clear = false,
        .size = pending.record.wanted,
        .relaunch = false,
        .closeDeadline = {},
        .terminated = false,
        .directory = pending.record.target.directory,
    });
    return pending.record.steamAppId.isEmpty() ? QString() : restartQuestion(pending.record.title);
}

bool WineDesktopHelper::restart(const QString &offer)
{
    for (Job &job : m_jobs) {
        if (job.offer == offer) {
            job.relaunch = true;
            job.closeDeadline = QDeadlineTimer(m_closeGrace);
            return true;
        }
    }
    return false;
}

QSize WineDesktopHelper::present(uint pid, const QString &windowClass)
{
    const std::optional<WineProcess> process = wineProcess(pid);
    if (!process) {
        return {};
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        return {};
    }
    const std::optional<WineDesktopRecord> record = m_records->find(wineRecordId(prefix->identity));
    if (!record || !record->written) {
        return {};
    }
    // The prefix may have lost the desktop since it was written: a Wine server
    // that overlapped the write saves its own copy when it exits, and Proton
    // rebuilds a prefix it is downgraded to. Then this run has none, and it is
    // written again after the run.
    const std::shared_ptr<WineDirectory> directory = WineDirectory::open(prefix->path, prefix->identity);
    if (directory && wineIsDesktop(wineDesktopValuesIn(*directory), *record->written)) {
        return *record->written;
    }
    const std::optional<pid_t> server = wineServerProcess(wineServerLockPath(prefix->temporaryDirectory, ::getuid(), prefix->identity));
    if (directory && server && !hasJob(record->id)) {
        startJob({
            .id = record->id,
            .offer = {},
            .game = static_cast<pid_t>(pid),
            .server = *server,
            .clear = false,
            .size = *record->written,
            .relaunch = false,
            .closeDeadline = {},
            .terminated = false,
            .directory = directory,
        });
    }
    return {};
}

bool WineDesktopHelper::hasJob(const QString &id) const
{
    return std::ranges::any_of(m_jobs, [&id](const Job &job) {
        return job.id == id;
    });
}

QList<WineDesktopRecord> WineDesktopHelper::prepared() const
{
    QList<WineDesktopRecord> records = m_records->all();
    records.removeIf([](const WineDesktopRecord &record) {
        return !record.written;
    });
    return records;
}

bool WineDesktopHelper::reset(const QString &id)
{
    const std::optional<WineDesktopRecord> record = m_records->find(id);
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
        .size = *record->written,
        .relaunch = false,
        .closeDeadline = {},
        .terminated = false,
        .directory = nullptr,
    });
    return true;
}

bool WineDesktopHelper::busy() const
{
    if (!m_jobs.isEmpty()) {
        return true;
    }
    return std::ranges::any_of(m_offers, [](const Pending &pending) {
        return !pending.expiry.hasExpired();
    });
}

void WineDesktopHelper::startJob(const Job &job)
{
    m_jobs.append(job);
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void WineDesktopHelper::poll()
{
    for (qsizetype index = 0; index < m_jobs.size();) {
        if (advance(m_jobs[index])) {
            m_jobs.removeAt(index);
        } else {
            ++index;
        }
    }
    if (m_jobs.isEmpty()) {
        m_timer.stop();
    }
}

bool WineDesktopHelper::stillRunning(Job &job)
{
    if (job.game > 0 && wineProcessExists(job.game)) {
        // The user chose to restart and was told that unsaved progress may be
        // lost; a game that did not close when its window was asked to is
        // asked to terminate, once.
        if (job.relaunch && !job.terminated && job.closeDeadline.hasExpired()) {
            ::kill(job.game, SIGTERM);
            job.terminated = true;
        }
        return true;
    }
    return job.server > 0 && wineProcessExists(job.server);
}

bool WineDesktopHelper::advance(Job &job)
{
    if (stillRunning(job)) {
        return false;
    }
    std::optional<WineDesktopRecord> record = m_records->find(job.id);
    if (!record) {
        return true;
    }
    record->target.directory = job.directory;
    const WineWriteResult result = job.clear ? wineClearDesktop(record->target, ::getuid(), job.size)
                                             : wineSetDesktop(record->target, ::getuid(), job.size, record->written, QDateTime::currentSecsSinceEpoch());
    if (result == WineWriteResult::Busy) {
        // A new run started meanwhile; wait for its server if the host sees it.
        job.server = hostServer(*record).value_or(0);
        return false;
    }
    settle(job, *record, result);
    Q_EMIT jobFinished(job.id, result);
    return true;
}

void WineDesktopHelper::settle(const Job &job, WineDesktopRecord &record, WineWriteResult result)
{
    if (result == WineWriteResult::Written && job.clear) {
        record.written.reset();
        record.never ? m_records->store(record) : m_records->remove(job.id);
        return;
    }
    if (result == WineWriteResult::Written) {
        record.written = job.size;
        m_records->store(record);
        if (job.relaunch && !m_launcher(record)) {
            qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "Could not start" << record.title << "again";
        }
        return;
    }
    qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "Left the prefix of" << record.title << "unchanged, result" << static_cast<int>(result);
    // The user has a desktop of their own there now; this one is theirs to
    // keep, and nothing of it is this companion's to undo any more.
    if (result == WineWriteResult::DesktopOfTheUser && !job.clear) {
        record.written.reset();
        m_records->store(record);
    }
}
