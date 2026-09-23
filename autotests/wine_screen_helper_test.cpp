/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lockholder.h"
#include "wineprocess.h"
#include "wineregistry.h"
#include "winescreenhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <unistd.h>

namespace
{

const QByteArray s_user = QByteArrayLiteral("WINE REGISTRY Version 2\n\n[Control Panel\\\\Desktop] 1789805658\n");
// A prefix that has run once and kept the devices it found.
const QByteArray s_machine = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0000&0000] 1790154356\n"
    "\"DeviceDesc\"=\"Generic Non-PnP Monitor\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000\\\\Device Parameters] 1790154350\n"
    "\"VideoID\"=\"{65aaded5-ba18-41e2-9572-7290231b645a}\"\n");
const QSize s_size(2560, 1440);
const QList<WineScreen> s_screens = {{.rect = QRect(QPoint(), s_size), .rate = 60}};
const QString s_class = QStringLiteral("steam_app_228380");

} // namespace

/*
 * The helper against real processes: a game is a process with Proton's
 * environment, and its Wine server is a process holding the lock below the
 * real /tmp, named after the test's prefix, so it meets nothing else there.
 */
class WineScreenHelperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void preparesTheGameAfterItsServerExits();
    void restartsTheGameWhenAsked();
    void presentsOnlyWhatItPrepared();
    void resetsItsDesktop();
    void remembersNever();
    void writesALostDesktopAgain();
    void followsANewSize();
    void followsANewRate();
    void takesBackAPreparationAGameWillNotStartWith();
    void undoesWhatIsNoLongerWanted();
    void stopsWhenTheGameKeepsItsOwnResolution();

private:
    std::unique_ptr<QProcess> startGame() const;
    std::unique_ptr<LockHolder> startServer() const;
    std::optional<QSize> screen() const;
    void writePrefix() const;
    QString acceptOffer(QProcess &game);

    std::optional<QTemporaryDir> m_root;
    QString m_prefix;
    QString m_lockDirectory;
    std::optional<WineScreenRecords> m_records;
    std::optional<WineScreenHelper> m_helper;
    QStringList m_launched;
};

void WineScreenHelperTest::init()
{
    if (!wineProcess(QCoreApplication::applicationPid())) {
        QSKIP("This platform cannot read another process's environment.");
    }
    m_root.emplace();
    QVERIFY(m_root->isValid());
    m_prefix = m_root->filePath(QStringLiteral("compatdata/228380/pfx"));
    QVERIFY(QDir().mkpath(m_prefix));
    writePrefix();
    const std::optional<WinePrefixIdentity> identity = winePrefixIdentity(m_prefix);
    QVERIFY(identity);
    m_lockDirectory = QFileInfo(wineServerLockPath(QStringLiteral("/tmp"), ::getuid(), *identity)).path();
    QVERIFY(QDir().mkpath(m_lockDirectory));
    m_records.emplace(m_root->filePath(QStringLiteral("records")));
    m_helper.emplace(&*m_records);
    m_helper->setTiming(std::chrono::milliseconds(20), std::chrono::milliseconds(200));
    m_launched.clear();
    m_helper->setLauncher([this](const WineScreenRecord &record) {
        m_launched.append(record.steamAppId);
        return true;
    });
}

void WineScreenHelperTest::cleanup()
{
    m_helper.reset();
    QDir(m_lockDirectory).removeRecursively();
}

std::unique_ptr<QProcess> WineScreenHelperTest::startGame() const
{
    auto game = std::make_unique<QProcess>();
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("WINEPREFIX"), m_prefix);
    environment.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"), m_root->filePath(QStringLiteral("compatdata/228380")));
    environment.insert(QStringLiteral("SteamAppId"), QStringLiteral("228380"));
    game->setProcessEnvironment(environment);
    game->start(QStringLiteral("sleep"), {QStringLiteral("60")});
    game->waitForStarted();
    // QProcess reports the start before the new program's environment is in
    // place; a real game has long been running when the effect asks.
    const bool ready = QTest::qWaitFor(
        [&game] {
        const std::optional<WineProcess> process = wineProcess(game->processId());
        return process && process->environment.contains(QStringLiteral("WINEPREFIX"));
    },
        5000);
    Q_UNUSED(ready)
    return game;
}

std::unique_ptr<LockHolder> WineScreenHelperTest::startServer() const
{
    return std::make_unique<LockHolder>(m_lockDirectory + QStringLiteral("/lock"));
}

std::optional<QSize> WineScreenHelperTest::screen() const
{
    QFile registry(m_prefix + QStringLiteral("/system.reg"));
    if (!registry.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QList<WineScreen> screens = wineScreens(registry.readAll());
    return screens.isEmpty() ? std::nullopt : std::optional(screens.first().rect.size());
}

// The prefix as Wine leaves it: the devices it found, and no screen of ours.
void WineScreenHelperTest::writePrefix() const
{
    for (const auto &[name, text] : {std::pair{QStringLiteral("/system.reg"), s_machine}, std::pair{QStringLiteral("/user.reg"), s_user}}) {
        QFile registry(m_prefix + name);
        QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Truncate));
        registry.write(text);
    }
}

QString WineScreenHelperTest::acceptOffer(QProcess &game)
{
    const WineScreenHelper::Offered offered = m_helper->offer(game.processId(), s_class, QStringLiteral("Wreckfest"), s_screens);
    if (offered.offer.isEmpty() || !offered.question.contains(QStringLiteral("2560 × 1440"))) {
        return {};
    }
    return m_helper->answer(offered.offer, QStringLiteral("accept")).isEmpty() ? QString() : offered.offer;
}

void WineScreenHelperTest::preparesTheGameAfterItsServerExits()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    auto server = startServer();
    QVERIFY(server->locked());
    const std::unique_ptr<QProcess> game = startGame();
    QVERIFY(!acceptOffer(*game).isEmpty());
    QTest::qWait(100);
    QVERIFY(finished.isEmpty());
    game->kill();
    game->waitForFinished();
    QTest::qWait(100);
    QVERIFY(finished.isEmpty());
    QCOMPARE(screen(), std::nullopt);
    server.reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(screen(), s_size);
    QVERIFY(m_launched.isEmpty());
    QVERIFY(!m_helper->busy());
}

void WineScreenHelperTest::restartsTheGameWhenAsked()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    auto server = startServer();
    const std::unique_ptr<QProcess> game = startGame();
    const QString offer = acceptOffer(*game);
    QVERIFY(m_helper->restart(offer));
    // The game ignored its window's close; after the grace period it is asked
    // to terminate.
    QTRY_COMPARE(game->state(), QProcess::NotRunning);
    server.reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(m_launched, QStringList{QStringLiteral("228380")});
}

void WineScreenHelperTest::presentsOnlyWhatItPrepared()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->present(game->processId(), s_class, s_screens), QSize());
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    auto server = startServer();
    const std::unique_ptr<QProcess> desktop = startGame();
    QCOMPARE(m_helper->present(desktop->processId(), s_class, s_screens), s_size);
    QCOMPARE(m_helper->present(desktop->processId(), QStringLiteral("steam_app_550"), s_screens), QSize());
    QCOMPARE(m_helper->offer(desktop->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
    QCOMPARE(m_helper->prepared().size(), 1);
}

void WineScreenHelperTest::resetsItsDesktop()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    const QString id = m_helper->prepared().value(0).id;
    auto server = startServer();
    QVERIFY(m_helper->reset(id));
    QTest::qWait(100);
    QCOMPARE(finished.size(), 1);
    QCOMPARE(screen(), s_size);
    server.reset();
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(m_helper->prepared().isEmpty());
    QVERIFY(!m_records->find(id));
}

void WineScreenHelperTest::remembersNever()
{
    auto server = startServer();
    const std::unique_ptr<QProcess> game = startGame();
    const WineScreenHelper::Offered first = m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_screens);
    QVERIFY(!first.offer.isEmpty());
    QCOMPARE(m_helper->answer(first.offer, QStringLiteral("never")), QString());
    QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
    const WineScreenHelper::Offered unknown = m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), {});
    QCOMPARE(unknown.offer, QString());
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(!m_helper->busy());
}

void WineScreenHelperTest::writesALostDesktopAgain()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    // A server that overlapped the write saved its own copy on exit.
    writePrefix();
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> desktop = startGame();
        QCOMPARE(m_helper->present(desktop->processId(), s_class, s_screens), QSize());
        QCOMPARE(m_helper->present(desktop->processId(), s_class, s_screens), QSize());
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(finished.last().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(screen(), s_size);
}

void WineScreenHelperTest::followsANewSize()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    {
        // This run is presented at the size it was prepared for, and the new
        // size is written after it.
        auto server = startServer();
        const std::unique_ptr<QProcess> desktop = startGame();
        QCOMPARE(m_helper->present(desktop->processId(), s_class, QList<WineScreen>{{.rect = QRect(0, 0, 1920, 1080), .rate = 60}}), s_size);
        QCOMPARE(screen(), s_size);
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), QSize(1920, 1080));
    QCOMPARE(m_helper->prepared().value(0).written, QSize(1920, 1080));
}

// The screen is described for the rate the output runs at, so a rate that
// changed is written again even though the size did not: the modes a game is
// offered are the ones its screen has.
void WineScreenHelperTest::followsANewRate()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        // The same size at another rate: this run is presented, and the screen
        // is described again after it.
        QCOMPARE(m_helper->present(game->processId(), s_class, QList<WineScreen>{{.rect = QRect(QPoint(), s_size), .rate = 120}}), s_size);
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), s_size);
    {
        // And at the rate it was written for, nothing is written again.
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->present(game->processId(), s_class, QList<WineScreen>{{.rect = QRect(QPoint(), s_size), .rate = 120}}), s_size);
        game->kill();
        game->waitForFinished();
    }
    QTest::qWait(500);
    QCOMPARE(finished.size(), 2);
}

// A game that will not start with the screen its prefix was described cannot be
// judged by what it draws, because it draws nothing. The run this companion
// started itself is watched instead: a server that comes and goes without the
// effect asking about a window of it takes the description back.
void WineScreenHelperTest::takesBackAPreparationAGameWillNotStartWith()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        const QString offer = acceptOffer(*game);
        QVERIFY(!offer.isEmpty());
        // The user chose to have it started again, so this companion watches
        // the run that follows.
        QVERIFY(m_helper->restart(offer));
        QTRY_COMPARE(game->state(), QProcess::NotRunning);
    }
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(screen(), s_size);
    QCOMPARE(m_launched, QStringList{QStringLiteral("228380")});
    {
        // The run that followed: its server came and went, and no window of it
        // was ever asked about.
        auto server = startServer();
        QTest::qWait(100);
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(m_helper->prepared().isEmpty());
    // And not offered again, until the settings page resets it.
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
    }
}

void WineScreenHelperTest::undoesWhatIsNoLongerWanted()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    {
        // The effect wants nothing smaller any more: nothing is presented, and
        // the desktop goes after this run.
        auto server = startServer();
        const std::unique_ptr<QProcess> desktop = startGame();
        QCOMPARE(m_helper->present(desktop->processId(), s_class, {}), QSize());
        QCOMPARE(screen(), s_size);
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(m_helper->prepared().isEmpty());
}

void WineScreenHelperTest::stopsWhenTheGameKeepsItsOwnResolution()
{
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(screen(), s_size);
    {
        // Asked again for the size the prefix already describes: the game does
        // not take it, so the description goes after this run and nothing is
        // offered.
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(m_helper->prepared().isEmpty());
    // And not offered again, until the settings page resets it.
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
    }
}

QTEST_GUILESS_MAIN(WineScreenHelperTest)

#include "wine_screen_helper_test.moc"
