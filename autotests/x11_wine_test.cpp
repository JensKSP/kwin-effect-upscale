/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_integration_test.h"
#include "x11_standin_game.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

void UpscaleX11IntegrationTest::leavesWineToTheProxy_data()
{
    QTest::addColumn<QString>("runtime");
    QTest::addColumn<bool>("fullscreenOnMap");
    QTest::addColumn<bool>("wine");
    QTest::newRow("wine-fullscreen-on-map") << QStringLiteral("wine") << true << true;
    QTest::newRow("wine64-fullscreen-later") << QStringLiteral("wine64") << false << true;
    QTest::newRow("preloader") << QStringLiteral("wine-preloader") << true << true;
    QTest::newRow("proton-preloader") << QStringLiteral("wine64-preloader") << true << true;
    // The same program under a name of its own, which is resized: what the
    // Wine rows prove is the effect leaving them alone, not a session in which
    // nothing would have been resized anyway.
    QTest::newRow("native-control") << QStringLiteral("game") << true << false;
}

// Wine takes its screen from its prefix when it starts, and resizing its running
// window fights that screen. A Wine program whose connection the X11 session
// proxy did not answer, as nothing does in this session, is therefore neither
// held at its first mapping nor resized afterwards, and the status says why.
void UpscaleX11IntegrationTest::leavesWineToTheProxy()
{
    QFETCH(QString, runtime);
    QFETCH(bool, fullscreenOnMap);
    QFETCH(bool, wine);
    // A real process under a Wine loader's name, which makes the window
    // itself. No Wine installation or prefix is involved.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.filePath(runtime);
    QVERIFY(QFile::copy(QStringLiteral(UPSCALE_TEST_X11_GAME), executable));
    configure(true);
    StandInGame game(executable, fullscreenOnMap ? QStringLiteral("on-map") : QStringLiteral("after-map"), QSize(3840, 2160));
    QVERIFY(game.started());
    QTRY_VERIFY(game.isFullscreen());
    const QString reason = QStringLiteral("told a smaller screen only by the X11 session proxy");
    if (!wine) {
        QTRY_VERIFY_WITH_TIMEOUT(game.configuredSizes().contains(QSize(1920, 1080)), 10000);
        QVERIFY2(!status().contains(reason), qPrintable(status()));
        return;
    }
    // The reason is the same test that keeps every request from this window,
    // so once it is reported nothing can have been asked of it, or will be.
    QTRY_VERIFY2(status().contains(reason), qPrintable(status()));
    QCOMPARE(game.geometry().size(), QSize(3840, 2160));
    // Held, it would have been made smaller inside its mapping.
    QCOMPARE(game.sizeAtMapping(), QSize(3840, 2160));
    QVERIFY(!game.configuredSizes().contains(QSize(1920, 1080)));
    QCOMPARE(game.closeRequests(), 0);
}
