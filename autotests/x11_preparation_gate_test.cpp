/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_prepared_test.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

void UpscaleX11PreparedTest::defersWineUntilPrepared_data()
{
    QTest::addColumn<QString>("runtime");
    QTest::addColumn<bool>("fullscreenOnMap");
    QTest::addColumn<bool>("available");
    QTest::addColumn<bool>("prepared");
    QTest::addColumn<bool>("disableBeforeReply");
    QTest::newRow("wine-fullscreen") << QStringLiteral("wine") << true << true << false << false;
    QTest::newRow("wine64-later-fullscreen") << QStringLiteral("wine64") << false << true << false << false;
    QTest::newRow("preloader-no-helper") << QStringLiteral("wine-preloader") << true << false << false << false;
    QTest::newRow("proton-prepared-record-native-buffer") << QStringLiteral("wine64-preloader") << true << true << true << false;
    QTest::newRow("disabled-before-helper-reply") << QStringLiteral("wine64") << true << true << false << true;
}

void UpscaleX11PreparedTest::defersWineUntilPrepared()
{
    QFETCH(QString, runtime);
    QFETCH(bool, fullscreenOnMap);
    QFETCH(bool, available);
    QFETCH(bool, prepared);
    QFETCH(bool, disableBeforeReply);
    // Give KWin a real process with a Wine loader basename, independently of
    // the game's class. No Wine installation or prefix mutation is involved.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.filePath(runtime);
    QVERIFY(QFile::copy(QStandardPaths::findExecutable(QStringLiteral("sleep")), executable));
    QProcess process;
    process.start(executable, {QStringLiteral("60")});
    QVERIFY(process.waitForStarted());
    const auto stop = qScopeGuard([&process]() {
        process.kill();
        process.waitForFinished();
    });
    TestHelper helper;
    if (disableBeforeReply) {
        helper.onPresent = [this]() {
            KConfigGroup entry(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-test"));
            entry.writeEntry("Enabled", false);
            entry.sync();
            m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
        };
    }
    if (prepared) {
        helper.size = QSize(1920, 1080);
    }
    if (available) {
        registerHelper(&helper);
    }
    const auto unregister = qScopeGuard([this, available]() {
        if (available) {
            unregisterHelper();
        }
    });
    {
        X11Client game(false);
        game.reportProcess(process.processId());
        QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160), fullscreenOnMap));
        if (!fullscreenOnMap) {
            game.fullscreen(true);
        }
        QTRY_VERIFY(game.isFullscreen());
        if (available) {
            QTRY_COMPARE(helper.asked.size(), disableBeforeReply ? 2 : 1);
            QCOMPARE(helper.asked.first(), uint(process.processId()));
            QCOMPARE(helper.wanted.first(), QSize(2560, 1440));
            if (disableBeforeReply) {
                QVERIFY(helper.wanted.last().isEmpty());
                QVERIFY(helper.offers.isEmpty());
            } else if (!prepared) {
                QTRY_COMPARE(helper.offers.size(), 1);
                QCOMPARE(helper.setupOffers, 1);
                QTRY_VERIFY(status().contains(QStringLiteral("Set this game up?")));
                press(Qt::Key_Return);
                QTRY_COMPARE(helper.answers, QStringList{QStringLiteral("offer-1 accept")});
                QTRY_VERIFY(status().contains(QStringLiteral("Restart it?")));
                press(Qt::Key_Escape);
                QVERIFY(helper.restarts.isEmpty());
            }
        }
        // Outlast ordinary resize validation. Neither a saved preparation nor
        // acceptance for a future launch may resize this native-size run.
        QTest::qWait(3500);
        QCOMPARE(game.geometry().size(), QSize(3840, 2160));
        QVERIFY(!game.configuredSizes().contains(QSize(2560, 1440)));
        QVERIFY(!game.configuredSizes().contains(QSize(1920, 1080)));
        QCOMPARE(game.closeRequests(), 0);
        if (prepared) {
            QVERIFY(helper.offers.isEmpty());
        }
    }
    if (prepared) {
        X11Client restarted(false);
        restarted.reportProcess(process.processId());
        QVERIFY(restarted.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
        QTRY_VERIFY(restarted.isFullscreen());
        QCOMPARE(restarted.geometry().size(), QSize(1920, 1080));
        QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    }
}
