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

// How long a write waits for a prefix that stays busy after its program has
// gone: long enough for the game to be played again in between, and not for
// ever, since a lock that cannot be tested also reads as busy.
constexpr std::chrono::hours busyLimit{12};

// How long the run this companion started itself is waited for before the watch
// on it is given up: Steam takes its time over a game, and a start the user
// cancelled leaves the preparation as it is.
constexpr std::chrono::minutes watchLimit{5};

// How long after the program and its server are gone the registry is written:
// a Wine server writes the registry out while it exits, and the lock it held
// goes at the very end of that.
constexpr std::chrono::seconds settleDelay{2};

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
                 "%1 does not take the resolution it is asked for.\n"
                 "Set it up to render at %2 from its next start?\n"
                 "The setting stays with the game until you undo it in the upscaler's settings.",
                 title,
                 sizeText(size));
}

QString restartQuestion(const QString &title)
{
    return i18nc("@info the offer shown in the middle of the screen; %1 is the game's window title",
                 "Restart %1 now to apply the change?\n"
                 "Unsaved progress may be lost.",
                 title);
}

WineDesktopTarget targetFor(const WinePrefix &prefix, const QProcessEnvironment &environment)
{
    // The paths the game used; the write checks that they still lead to the
    // directory proven now.
    return {
        .prefix = prefix.gamePath,
        .identity = prefix.identity,
        .steamCompatData = prefix.steamCompatDataPath.isEmpty() ? QString() : QDir::cleanPath(environment.value(QStringLiteral("STEAM_COMPAT_DATA_PATH"))),
        .temporaryDirectory = hostTemporaryDirectory,
        .directory = nullptr,
    };
}

bool holdsWhatWasWritten(const WineDesktopRecord &record, bool clear, const QList<WineScreen> &screens)
{
    if (!record.target.directory) {
        return true;
    }
    const QList<WineScreen> described = wineScreensIn(*record.target.directory);
    return clear ? described.isEmpty() : described == screens;
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

// The prefix of a running program, its server and the record kept for it, or
// nothing when any of the proofs fails.
std::optional<WineDesktopHelper::Pending> WineDesktopHelper::locate(uint pid, const QString &windowClass, const QString &title)
{
    const std::optional<WineProcess> process = wineProcess(pid);
    if (!process) {
        return std::nullopt;
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "No provable Wine prefix for process" << pid << "reason" << static_cast<int>(prefix.error());
        return std::nullopt;
    }
    // Waiting for this process is what makes the later write safe. A server
    // in a process namespace this helper cannot see has no number here, and
    // then nothing is offered.
    const std::optional<pid_t> server = wineServerProcess(wineServerLockPath(prefix->temporaryDirectory, ::getuid(), prefix->identity));
    if (!server) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "The Wine server of process" << pid << "is out of sight";
        return std::nullopt;
    }
    // Held from now on: after the game has gone, it still reaches the
    // directory proven now, whatever paths the host has.
    const std::shared_ptr<WineDirectory> directory = WineDirectory::open(prefix->path, prefix->identity);
    if (!directory) {
        return std::nullopt;
    }
    const QString id = wineRecordId(prefix->identity);
    WineDesktopRecord record = m_records->find(id).value_or(WineDesktopRecord{});
    record.id = id;
    record.title = title;
    record.target = targetFor(*prefix, process->environment);
    record.target.directory = directory;
    record.steamAppId = prefix->steamAppId;
    return Pending{.record = record, .game = static_cast<pid_t>(pid), .server = *server, .screens = {}, .expiry = QDeadlineTimer(offerLifetime)};
}

WineDesktopHelper::Offered WineDesktopHelper::offer(uint pid, const QString &windowClass, const QString &title, const QList<WineScreen> &screens)
{
    const QSize size = screens.value(0).rect.size();
    std::optional<Pending> pending = size.isEmpty() ? std::nullopt : locate(pid, windowClass, title);
    if (!pending) {
        return {};
    }
    pending->screens = screens;
    WineDesktopRecord &record = pending->record;
    if (record.never) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Nothing offered for" << title << "prefix" << record.id << ": the answer was never";
        return {};
    }
    if (record.written == size) {
        // The prefix already describes a screen of exactly this size and the
        // game still draws at the output's. Such a game renders at a size of
        // its own whatever the screen offers, which is beyond what this
        // companion can reach: the description is taken away again after this
        // run and not offered for this game again; a reset on the settings page
        // asks anew.
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << title << "keeps its own resolution although its prefix was prepared for" << size
                                         << "; undoing that and not asking again";
        record.never = true;
        m_records->store(record);
        afterRun(record, pid, pending->server, record.target.directory, {});
        return {};
    }
    record.wanted = size;
    qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Offering" << sizeText(size) << "to" << title << "prefix" << record.id << "server" << pending->server;
    const QString offer = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_offers.insert(offer, *pending);
    return {.offer = offer, .question = question(title, size)};
}

QString WineDesktopHelper::answer(const QString &offer, const QString &answer)
{
    const Pending pending = m_offers.take(offer);
    if (pending.record.id.isEmpty() || pending.expiry.hasExpired()) {
        return {};
    }
    qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Answer" << answer << "for" << pending.record.title << "prefix" << pending.record.id;
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
        .screens = pending.screens,
        .relaunch = false,
        .closeDeadline = {},
        .terminated = false,
        .directory = pending.record.target.directory,
        .giveUp = std::nullopt,
        .settled = std::nullopt,
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

QSize WineDesktopHelper::present(uint pid, const QString &windowClass, const QList<WineScreen> &wanted)
{
    const std::optional<WineProcess> process = wineProcess(pid);
    if (!process) {
        return {};
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        return {};
    }
    std::optional<WineDesktopRecord> record = m_records->find(wineRecordId(prefix->identity));
    if (!record || !record->written) {
        qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Nothing prepared for prefix" << wineRecordId(prefix->identity) << prefix->path;
        return {};
    }
    const std::shared_ptr<WineDirectory> directory = WineDirectory::open(prefix->path, prefix->identity);
    const std::optional<pid_t> server = wineServerProcess(wineServerLockPath(prefix->temporaryDirectory, ::getuid(), prefix->identity));
    if (!directory || !server) {
        return {};
    }
    proven(record->id);
    const QSize written = *record->written;
    const QSize wantedSize = wanted.value(0).rect.size();
    // The prefix may have lost the description since it was written: a Wine
    // server that overlapped the write saves its own copy when it exits, and
    // Proton rebuilds a prefix it is downgraded to. Then this run has none.
    const bool held = wineScreensIn(*directory).value(0).rect.size() == written;
    qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Asked about prefix" << record->id << "of" << record->title << ": wants"
                                     << wineScreensText(wanted) << "written" << written << "screen described in prefix" << held;
    if (wantedSize.isEmpty()) {
        // The effect no longer wants this program smaller: undone after this
        // run, and this run is left as it is. A description the prefix has lost
        // already leaves nothing to undo but the record of it.
        if (held) {
            afterRun(*record, pid, *server, directory, {});
        } else if (!hasJob(record->id)) {
            record->written.reset();
            record->never ? m_records->store(*record) : m_records->remove(record->id);
        }
        return {};
    }
    if (!held || wantedSize != written || wineScreensText(wanted) != record->described) {
        // Described again after this run: at the size the effect wants now, or
        // for the screens the session has now.
        record->wanted = wantedSize;
        m_records->store(*record);
        afterRun(*record, pid, *server, directory, wanted);
    }
    return held ? written : QSize();
}

// Queues the write that follows this run of the program: those screens, or,
// without any, the description taken away again.
void WineDesktopHelper::afterRun(const WineDesktopRecord &record, uint pid, pid_t server, const std::shared_ptr<WineDirectory> &directory,
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

bool WineDesktopHelper::busy() const
{
    if (!m_jobs.isEmpty() || !m_probation.isEmpty()) {
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

// The run that followed a preparation this companion started. Its server coming
// and going without the effect asking about a window of it is a game that will
// not start with the screen it was described; the description goes, and it is
// not offered again.
bool WineDesktopHelper::watch(Probation &probation)
{
    const std::optional<WineDesktopRecord> record = m_records->find(probation.id);
    if (!record || !record->written) {
        return true;
    }
    if (hostServer(*record)) {
        probation.started = true;
        return false;
    }
    if (!probation.started) {
        // Still waiting for the game to come up; Steam takes its time, and a
        // start the user cancelled leaves the preparation as it is.
        return probation.expiry.hasExpired();
    }
    qCWarning(KWIN_UPSCALE_WINEDESKTOP) << record->title << "ran and ended without drawing a window after its prefix was described for"
                                        << *record->written << "; taking that back and not offering it again";
    WineDesktopRecord failed = *record;
    failed.never = true;
    m_records->store(failed);
    afterRun(failed, 0, 0, nullptr, {});
    return true;
}

// The effect asked about a window of this prefix, so its program starts and
// draws with the screen it was described.
void WineDesktopHelper::proven(const QString &id)
{
    m_probation.removeIf([&id](const Probation &probation) {
        return probation.id == id;
    });
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
        job.settled.reset();
        return false;
    }
    if (!job.settled) {
        job.settled = QDeadlineTimer(settleDelay);
    }
    if (!job.settled->hasExpired()) {
        return false;
    }
    std::optional<WineDesktopRecord> record = m_records->find(job.id);
    if (!record) {
        return true;
    }
    record->target.directory = job.directory;
    if (!job.clear) {
        // The description is what a Wine build reads before it asks the display
        // server, and this companion was tested against one. A build it has not
        // seen is written for all the same, since every build after this one
        // would otherwise lose the feature, and named here so that a report
        // about such a build says which it was.
        const QString build = wineBuild(record->target.steamCompatData);
        if (!wineBuildTested(build)) {
            qCInfo(KWIN_UPSCALE_WINEDESKTOP) << "Describing the screen in a prefix of"
                                             << (build.isEmpty() ? QStringLiteral("a Wine build this companion cannot tell") : build)
                                             << ", which it was not tested against; a screen it does not read leaves the game at full size";
        }
    }
    WineWriteResult result = job.clear ? wineClearScreen(record->target, ::getuid())
                                       : wineSetScreens(record->target, ::getuid(), job.screens, QDateTime::currentSecsSinceEpoch());
    // What was written is read back: a server that wrote its own copy over it
    // leaves the prefix without the change, and then it is written again after
    // the next run rather than reported as done.
    if (result == WineWriteResult::Written && !holdsWhatWasWritten(*record, job.clear, job.screens)) {
        qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "The prefix of" << record->title << "did not keep what was written";
        result = WineWriteResult::WrittenMeanwhile;
    }
    if (result == WineWriteResult::WrittenMeanwhile) {
        // Recorded as written, so that the next write knows it as this
        // companion's own, and written again once that server has gone.
        record->written = job.clear ? std::nullopt : std::optional<QSize>(job.screens.value(0).rect.size());
        record->described = job.clear ? QString() : wineScreensText(job.screens);
        m_records->store(*record);
    }
    if (result == WineWriteResult::Busy || result == WineWriteResult::WrittenMeanwhile) {
        // A new run started meanwhile; wait for its server if the host sees it.
        job.server = hostServer(*record).value_or(0);
        if (!job.giveUp) {
            job.giveUp = QDeadlineTimer(busyLimit);
        }
        if (!job.giveUp->hasExpired()) {
            return false;
        }
        qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "Gave up waiting for the prefix of" << record->title;
    }
    settle(job, *record, result);
    Q_EMIT jobFinished(job.id, result);
    return true;
}

void WineDesktopHelper::settle(const Job &job, WineDesktopRecord &record, WineWriteResult result)
{
    const QList<WineScreen> described = record.target.directory ? wineScreensIn(*record.target.directory) : QList<WineScreen>{};
    qCInfo(KWIN_UPSCALE_WINEDESKTOP) << (job.clear ? "Undo of" : "Preparation of") << record.title << wineScreensText(job.screens) << "result"
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
            qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "Could not start" << record.title << "again";
        } else if (job.relaunch) {
            // The run this companion started is watched: see Probation.
            m_probation.append({.id = job.id, .started = false, .expiry = QDeadlineTimer(watchLimit)});
            m_timer.start();
        }
        return;
    }
    qCWarning(KWIN_UPSCALE_WINEDESKTOP) << "Left the prefix of" << record.title << "unchanged, result" << static_cast<int>(result);
    // The user runs the prefix in a virtual desktop of their own now; that is
    // theirs to keep, and there is nothing of ours left in it to undo.
    if (result == WineWriteResult::DesktopOfTheUser && !job.clear) {
        record.written.reset();
        m_records->store(record);
    }
}
