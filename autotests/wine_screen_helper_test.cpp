/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wine_screen_helper_test.h"

namespace
{

const QSize s_size(2560, 1440);
const QList<WineScreen> s_screens = {{.rect = QRect(QPoint(), s_size), .rate = 60}};
const QString s_class = QStringLiteral("steam_app_228380");

} // namespace

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

void WineScreenHelperTest::restartsTheGameWhenAsked_data()
{
    QTest::addColumn<QList<WineScreen>>("screens");
    QTest::newRow("unchanged") << s_screens;
    QTest::newRow("unknown-rate") << QList<WineScreen>{{.rect = QRect(QPoint(), s_size), .rate = 0}};
    QTest::newRow("fewer-monitors") << QList<WineScreen>{s_screens.first(), {.rect = QRect(2560, 0, 1920, 1080), .rate = 60}};
}

void WineScreenHelperTest::restartsTheGameWhenAsked()
{
    QFETCH(QList<WineScreen>, screens);
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    auto server = startServer();
    const std::unique_ptr<QProcess> game = startGame();
    const WineScreenHelper::Offered offered = m_helper->offer(game->processId(), s_class, QStringLiteral("Wreckfest"), screens);
    QVERIFY(!offered.offer.isEmpty());
    QVERIFY(!m_helper->answer(offered.offer, QStringLiteral("accept")).isEmpty());
    QVERIFY(m_helper->restart(offered.offer));
    // The game ignored its window's close; after the grace period it is asked
    // to terminate.
    QTRY_COMPARE(game->state(), QProcess::NotRunning);
    server.reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(screen(), s_size);
    QCOMPARE(m_launched, QStringList{QStringLiteral("228380")});
    server = startServer();
    const std::unique_ptr<QProcess> restarted = startGame();
    QCOMPARE(m_helper->present(restarted->processId(), s_class, screens), s_size);
    QVERIFY(!m_helper->busy());
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

void WineScreenHelperTest::resetsItsDesktop_data()
{
    QTest::addColumn<bool>("userDesktop");
    QTest::newRow("unchanged") << false;
    QTest::newRow("user-enabled-desktop") << true;
}

void WineScreenHelperTest::resetsItsDesktop()
{
    QFETCH(bool, userDesktop);
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
    QByteArray users;
    if (userDesktop) {
        QFile user(m_prefix + QStringLiteral("/user.reg"));
        QVERIFY(user.open(QIODevice::ReadWrite));
        users = user.readAll() + "\n[Software\\\\Wine\\\\Explorer] 1789805658\n\"Desktop\"=\"shell\"\n";
        user.seek(0);
        QCOMPARE(user.write(users), users.size());
        user.close();
        {
            auto server = startServer();
            const std::unique_ptr<QProcess> game = startGame();
            QCOMPARE(m_helper->present(game->processId(), s_class, QList<WineScreen>{{.rect = QRect(0, 0, 1920, 1080), .rate = 60}}), s_size);
            game->kill();
            game->waitForFinished();
        }
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.last().at(1).value<WineWriteResult>(), WineWriteResult::DesktopOfTheUser);
        QCOMPARE(m_helper->prepared().value(0).written, s_size);
    }
    finished.clear();
    auto server = startServer();
    QVERIFY(m_helper->reset(id));
    QTest::qWait(100);
    QCOMPARE(finished.size(), 0);
    QCOMPARE(screen(), s_size);
    server.reset();
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.last().at(1).value<WineWriteResult>(), WineWriteResult::Written);
    QCOMPARE(screen(), std::nullopt);
    QVERIFY(m_helper->prepared().isEmpty());
    QVERIFY(!m_records->find(id));
    if (userDesktop) {
        QFile user(m_prefix + QStringLiteral("/user.reg"));
        QVERIFY(user.open(QIODevice::ReadOnly));
        QCOMPARE(user.readAll(), users);
    }
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

void WineScreenHelperTest::writesALostDesktopAgain_data()
{
    QTest::addColumn<bool>("presentFirst");
    QTest::newRow("after-presentation") << true;
    QTest::newRow("offer-only") << false;
}

void WineScreenHelperTest::writesALostDesktopAgain()
{
    QFETCH(bool, presentFirst);
    QSignalSpy finished(&*m_helper, &WineScreenHelper::jobFinished);
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> game = startGame();
        QVERIFY(!acceptOffer(*game).isEmpty());
        game->kill();
        game->waitForFinished();
    }
    QTRY_COMPARE(finished.size(), 1);
    const QString id = m_helper->prepared().first().id;
    // A server that overlapped the write saved its own copy on exit.
    writePrefix();
    {
        auto server = startServer();
        const std::unique_ptr<QProcess> desktop = startGame();
        if (presentFirst) {
            QCOMPARE(m_helper->present(desktop->processId(), s_class, s_screens), QSize());
        }
        QCOMPARE(m_helper->offer(desktop->processId(), s_class, QStringLiteral("Wreckfest"), s_screens).offer, QString());
        QVERIFY(!m_records->find(id)->never);
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
