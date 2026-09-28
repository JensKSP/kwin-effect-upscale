/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A game that disappears without warning: glmark2, built by
// tools/prepare-crash-game.py to crash where UPSCALE_TEST_CRASH says (see
// glmark2_crash.h). Members of the same test class. Where that build does not
// exist, UPSCALE_TEST_CRASH_GAME is unset and these cases skip; sessions.cmake
// starts a session for them where it does.

#include "crash_game.h"
#include "integration_test.h"

#include <QScopeGuard>

// The effect told to act on the crashing glmark2 with Auto, which tells it a
// smaller mode as it binds its outputs and asks its window for a scale later.
void UpscaleIntegrationTest::startCrashGame()
{
    QTRY_VERIFY(m_effects.isValid());
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    writeCatalogue(QStringLiteral("[Application-crashgame]\nName=Crashing game\nExecutable=.*/glmark2-wayland\n"
                                  "ExecutableMatch=RegularExpression\nMethodWaylandFullScreen=Auto\n"
                                  "MinimumPixels=0\nOrder=1\n"));
    configureResolution(true, false, Stored::Quality);
}

void UpscaleIntegrationTest::aGameThatCrashesIsLetGo_data()
{
    QTest::addColumn<QString>("point");
    QTest::newRow("after-binding-its-outputs") << QStringLiteral("bind");
    QTest::newRow("before-its-first-frame") << QStringLiteral("window");
}

// A game told a smaller mode that crashes before it has drawn anything: right
// after it bound its outputs, or once its window is configured but before its
// first frame. The effect keeps nothing for it afterwards, the next game is
// told as the first was, and giving the mode back passes over both.
void UpscaleIntegrationTest::aGameThatCrashesIsLetGo()
{
    if (crashGame().isEmpty()) {
        QSKIP("needs the crashing glmark2 from tools/prepare-crash-game.py");
    }
    QFETCH(QString, point);
    const auto restore = qScopeGuard([this]() {
        stopAdvertising();
    });
    startCrashGame();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString before = crashRecords(status());
    const QString told = QStringLiteral("UPSCALE_TEST_CRASH %1 85x85").arg(point);
    QCOMPARE(crashAt(QStringLiteral("glmark2-wayland"), point), told);
    QTRY_COMPARE(crashRecords(status()), before);
    QCOMPARE(crashAt(QStringLiteral("glmark2-wayland"), point), told);
    QTRY_COMPARE(crashRecords(status()), before);
    configureResolution(false, false, Stored::Quality);
    QCOMPARE(crashRecords(status()), before);
}

// Games that crash one after another, each once it has drawn a few frames, so
// that everything the effect arranges for a game it knows has happened: the
// mode told at connection, the window observed, its scale asked for. However
// many come and go, the effect keeps no more than it did before the first.
void UpscaleIntegrationTest::crashingGamesLeaveNothingBehind()
{
    if (crashGame().isEmpty()) {
        QSKIP("needs the crashing glmark2 from tools/prepare-crash-game.py");
    }
    const auto restore = qScopeGuard([this]() {
        stopAdvertising();
    });
    startCrashGame();
    if (QTest::currentTestFailed()) {
        return;
    }
    const QString before = crashRecords(status());
    for (int run = 0; run < 50; ++run) {
        const QString crashed = crashAt(QStringLiteral("glmark2-wayland"), QStringLiteral("frame:3"));
        QVERIFY2(crashed.startsWith(QStringLiteral("UPSCALE_TEST_CRASH frame")), qPrintable(crashed));
    }
    QTRY_COMPARE(crashRecords(status()), before);
}
