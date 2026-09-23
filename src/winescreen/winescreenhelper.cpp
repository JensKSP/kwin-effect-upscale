/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winescreenhelper.h"
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

Q_LOGGING_CATEGORY(KWIN_UPSCALE_WINESCREEN, "kwin.upscale.winescreen", QtInfoMsg)

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

bool launchThroughSteam(const WineScreenRecord &record)
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

WineScreenTarget targetFor(const WinePrefix &prefix, const QProcessEnvironment &environment)
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

bool holdsWhatWasWritten(const WineScreenRecord &record, bool clear, const QList<WineScreen> &screens)
{
    if (!record.target.directory) {
        return true;
    }
    const QList<WineScreen> described = wineScreensIn(*record.target.directory);
    return clear ? described.isEmpty() : described == screens;
}

std::optional<pid_t> hostServer(const WineScreenRecord &record)
{
    return wineServerProcess(wineServerLockPath(record.target.temporaryDirectory, ::getuid(), record.target.identity));
}

} // namespace

WineScreenHelper::WineScreenHelper(WineScreenRecords *records, QObject *parent)
    : QObject(parent)
    , m_records(records)
    , m_launcher(launchThroughSteam)
{
    m_timer.setInterval(std::chrono::seconds(1));
    connect(&m_timer, &QTimer::timeout, this, &WineScreenHelper::poll);
}

void WineScreenHelper::setLauncher(const Launcher &launcher)
{
    m_launcher = launcher;
}

void WineScreenHelper::setTiming(std::chrono::milliseconds poll, std::chrono::milliseconds closeGrace)
{
    m_timer.setInterval(poll);
    m_closeGrace = closeGrace;
}

// The prefix of a running program, its server and the record kept for it, or
// nothing when any of the proofs fails.
std::optional<WineScreenHelper::Pending> WineScreenHelper::locate(uint pid, const QString &windowClass, const QString &title)
{
    const std::optional<WineProcess> process = wineProcess(pid);
    if (!process) {
        return std::nullopt;
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        qCInfo(KWIN_UPSCALE_WINESCREEN) << "No provable Wine prefix for process" << pid << "reason" << static_cast<int>(prefix.error());
        return std::nullopt;
    }
    // Waiting for this process is what makes the later write safe. A server
    // in a process namespace this helper cannot see has no number here, and
    // then nothing is offered.
    const std::optional<pid_t> server = wineServerProcess(wineServerLockPath(prefix->temporaryDirectory, ::getuid(), prefix->identity));
    if (!server) {
        qCInfo(KWIN_UPSCALE_WINESCREEN) << "The Wine server of process" << pid << "is out of sight";
        return std::nullopt;
    }
    // Held from now on: after the game has gone, it still reaches the
    // directory proven now, whatever paths the host has.
    const std::shared_ptr<WineDirectory> directory = WineDirectory::open(prefix->path, prefix->identity);
    if (!directory) {
        return std::nullopt;
    }
    const QString id = wineRecordId(prefix->identity);
    WineScreenRecord record = m_records->find(id).value_or(WineScreenRecord{});
    record.id = id;
    record.title = title;
    record.target = targetFor(*prefix, process->environment);
    record.target.directory = directory;
    record.steamAppId = prefix->steamAppId;
    return Pending{.record = record, .game = static_cast<pid_t>(pid), .server = *server, .screens = {}, .expiry = QDeadlineTimer(offerLifetime)};
}

WineScreenHelper::Offered WineScreenHelper::offer(uint pid, const QString &windowClass, const QString &title, const QList<WineScreen> &screens)
{
    const QSize size = screens.value(0).rect.size();
    std::optional<Pending> pending = size.isEmpty() ? std::nullopt : locate(pid, windowClass, title);
    if (!pending) {
        return {};
    }
    pending->screens = screens;
    WineScreenRecord &record = pending->record;
    if (record.never) {
        qCInfo(KWIN_UPSCALE_WINESCREEN) << "Nothing offered for" << title << "prefix" << record.id << ": the answer was never";
        return {};
    }
    if (record.written == size) {
        // The prefix already describes a screen of exactly this size and the
        // game still draws at the output's. Such a game renders at a size of
        // its own whatever the screen offers, which is beyond what this
        // companion can reach: the description is taken away again after this
        // run and not offered for this game again; a reset on the settings page
        // asks anew.
        qCInfo(KWIN_UPSCALE_WINESCREEN) << title << "keeps its own resolution although its prefix was prepared for" << size
                                        << "; undoing that and not asking again";
        record.never = true;
        m_records->store(record);
        afterRun(record, pid, pending->server, record.target.directory, {});
        return {};
    }
    record.wanted = size;
    qCInfo(KWIN_UPSCALE_WINESCREEN) << "Offering" << sizeText(size) << "to" << title << "prefix" << record.id << "server" << pending->server;
    const QString offer = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_offers.insert(offer, *pending);
    return {.offer = offer, .question = question(title, size)};
}

QString WineScreenHelper::answer(const QString &offer, const QString &answer)
{
    const Pending pending = m_offers.take(offer);
    if (pending.record.id.isEmpty() || pending.expiry.hasExpired()) {
        return {};
    }
    qCInfo(KWIN_UPSCALE_WINESCREEN) << "Answer" << answer << "for" << pending.record.title << "prefix" << pending.record.id;
    if (answer == neverAnswer) {
        WineScreenRecord record = pending.record;
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

bool WineScreenHelper::restart(const QString &offer)
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

QSize WineScreenHelper::present(uint pid, const QString &windowClass, const QList<WineScreen> &wanted)
{
    const std::optional<WineProcess> process = wineProcess(pid);
    if (!process) {
        return {};
    }
    const WineLocated prefix = wineLocatePrefix(*process, ::getuid(), windowClass);
    if (!prefix) {
        return {};
    }
    std::optional<WineScreenRecord> record = m_records->find(wineRecordId(prefix->identity));
    if (!record || !record->written) {
        qCInfo(KWIN_UPSCALE_WINESCREEN) << "Nothing prepared for prefix" << wineRecordId(prefix->identity) << prefix->path;
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
    qCInfo(KWIN_UPSCALE_WINESCREEN) << "Asked about prefix" << record->id << "of" << record->title << ": wants"
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
