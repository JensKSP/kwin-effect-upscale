/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lockholder.h"
#include "winedesktophelper.h"
#include "wineprocess.h"
#include "wineregistry.h"

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

const QByteArray s_registry = QByteArrayLiteral("WINE REGISTRY Version 2\n\n[Control Panel\\\\Desktop] 1789805658\n");
const QSize s_size(2560, 1440);
const QString s_class = QStringLiteral("steam_app_228380");

} // namespace

/*
 * The helper against real processes: a game is a process with Proton's
 * environment, and its Wine server is a process holding the lock below the
 * real /tmp, named after the test's prefix, so it meets nothing else there.
 */
class WineDesktopHelperTest : public QObject
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
    void undoesWhatIsNoLongerWanted();
    void stopsWhenTheGameKeepsItsOwnResolution();

private:
    std::unique_ptr<QProcess> startGame() const;
    std::unique_ptr<LockHolder> startServer() const;
    WineDesktopValues values() const;
    QString acceptOffer(QProcess &game);

    std::optional<QTemporaryDir> m_root;
    QString m_prefix;
    QString m_lockDirectory;
    std::optional<WineDesktopRecords> m_records;
    std::optional<WineDesktopHelper> m_helper;
    QStringList m_launched;
};

void WineDesktopHelperTest::init()
{
    if (!wineProcess(QCoreApplication::applicationPid())) {
        QSKIP("This platform cannot read another process's environment.");
    }
    m_root.emplace();
    QVERIFY(m_root->isValid());
    m_prefix = m_root->filePath(QStringLiteral("compatdata/228380/pfx"));
    QVERIFY(QDir().mkpath(m_prefix));
    QFile registry(m_prefix + QStringLiteral("/user.reg"));
    QVERIFY(registry.open(QIODevice::WriteOnly));
    registry.write(s_registry);
    registry.close();
    const std::optional<WinePrefixIdentity> identity = winePrefixIdentity(m_prefix);
    QVERIFY(identity);
    m_lockDirectory = QFileInfo(wineServerLockPath(QStringLiteral("/tmp"), ::getuid(), *identity)).path();
    QVERIFY(QDir().mkpath(m_lockDirectory));
    m_records.emplace(m_root->filePath(QStringLiteral("records")));
    m_helper.emplace(&*m_records);
    m_helper->setTiming(std::chrono::milliseconds(20), std::chrono::milliseconds(200));
    m_launched.clear();
    m_helper->setLauncher([this](const WineDesktopRecord &record) {
        m_launched.append(record.steamAppId);
        return true;
    });
}

void WineDesktopHelperTest::cleanup()
{
    m_helper.reset();
    QDir(m_lockDirectory).removeRecursively();
}

std::unique_ptr<QProcess> WineDesktopHelperTest::startGame() const
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

std::unique_ptr<LockHolder> WineDesktopHelperTest::startServer() const
{
    return std::make_unique<LockHolder>(m_lockDirectory + QStringLiteral("/lock"));
}

WineDesktopValues WineDesktopHelperTest::values() const
{
    QFile registry(m_prefix + QStringLiteral("/user.reg"));
    return registry.open(QIODevice::ReadOnly) ? wineDesktopValues(registry.readAll()) : WineDesktopValues{};
}

QString WineDesktopHelperTest::acceptOffer(QProcess &game)
{
    const WineDesktopHelper::Offered offered = m_helper->offer(game.processId(), s_class, QStringLiteral("Wreckfest"), s_size);
    if (offered.offer.isEmpty() || !offered.question.contains(QStringLiteral("2560 × 1440"))) {
        return {};
    }
    return m_helper->answer(offered.offer, QStringLiteral("accept")).isEmpty() ? QString() : offered.offer;
}

void WineDesktopHelperTest::preparesTheGameAfterItsServerExits()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
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
    QCOMPARE(values(), WineDesktopValues{});
    server.reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(values().defaultSize, QStringLiteral("2560x1440"));
    QVERIFY(m_launched.isEmpty());
    QVERIFY(!m_helper->busy());
}

void WineDesktopHelperTest::restartsTheGameWhenAsked()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
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

void WineDesktopHelperTest::presentsOnlyWhatItPrepared()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->present(game->processId(), s_class, s_size), QSize());
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    auto server = startServer();
    const std::unique_ptr<QProcess> desktop = startGame();
    QCOMPARE(m_helper->present(desktop->processId(), s_class, s_size), s_size);
    QCOMPARE(m_helper->present(desktop->processId(), QStringLiteral("steam_app_550"), s_size), QSize());
    QCOMPARE(m_helper->offer(desktop->processId(), s_class, QStringLiteral("Wreckfest"), s_size).offer, QString());
    QCOMPARE(m_helper->prepared().size(), 1);
}

void WineDesktopHelperTest::resetsItsDesktop()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
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
    QCOMPARE(values().desktop, QStringLiteral("Default"));
    server.reset();
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(values(), WineDesktopValues{});
    QVERIFY(m_helper->prepared().isEmpty());
    QVERIFY(!m_records->find(id));
}

void WineDesktopHelperTest::remembersNever()
{
    auto server = startServer();
    const std::unique_ptr<QProcess> game = startGame();
    const WineDesktopHelper::Offered first = m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_size);
    QVERIFY(!first.offer.isEmpty());
    QCOMPARE(m_helper->answer(first.offer, QStringLiteral("never")), QString());
    QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_size).offer, QString());
    const WineDesktopHelper::Offered unknown = m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), QSize());
    QCOMPARE(unknown.offer, QString());
    QCOMPARE(values(), WineDesktopValues{});
    QVERIFY(!m_helper->busy());
}

void WineDesktopHelperTest::writesALostDesktopAgain()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    // A server that overlapped the write saved its own copy on exit.
    QFile registry(m_prefix + QStringLiteral("/user.reg"));
    QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Truncate));
    registry.write(s_registry);
    registry.close();
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> desktop = startGame();
        QCOMPARE(m_helper->present(desktop->processId(), s_class, s_size), QSize());
        QCOMPARE(m_helper->present(desktop->processId(), s_class, s_size), QSize());
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(finished.last().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(values().defaultSize, QStringLiteral("2560x1440"));
}

void WineDesktopHelperTest::followsANewSize()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
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
        QCOMPARE(m_helper->present(desktop->processId(), s_class, QSize(1920, 1080)), s_size);
        QCOMPARE(values().defaultSize, QStringLiteral("2560x1440"));
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(values().defaultSize, QStringLiteral("1920x1080"));
    QCOMPARE(m_helper->prepared().value(0).written, QSize(1920, 1080));
}

void WineDesktopHelperTest::undoesWhatIsNoLongerWanted()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
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
        QCOMPARE(m_helper->present(desktop->processId(), s_class, QSize()), QSize());
        QCOMPARE(values().desktop, QStringLiteral("Default"));
        desktop->kill();
        desktop->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(values(), WineDesktopValues{});
    QVERIFY(m_helper->prepared().isEmpty());
}

void WineDesktopHelperTest::stopsWhenTheGameKeepsItsOwnResolution()
{
    QSignalSpy finished(&*m_helper, &WineDesktopHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(values().defaultSize, QStringLiteral("2560x1440"));
    {
        // Asked again for the size the prefix already holds: the game does not
        // take it, so the desktop goes after this run and nothing is offered.
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_size).offer, QString());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(values(), WineDesktopValues{});
    QVERIFY(m_helper->prepared().isEmpty());
    // And not offered again, until the settings page resets it.
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QCOMPARE(m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), s_size).offer, QString());
    }
}

QTEST_GUILESS_MAIN(WineDesktopHelperTest)

#include "wine_desktop_helper_test.moc"
