/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A game that disappears without warning while the effect resizes its X11
// window: glmark2, built by tools/prepare-crash-game.py. Where that build does
// not exist, UPSCALE_TEST_CRASH_GAME is unset and this case skips;
// sessions.cmake starts a session with one output for it where it does, so
// that the root window glmark2 sizes itself by is the screen it fills.

#include "crash_game.h"
#include "x11_integration_test.h"

#include <KSharedConfig>

#include <QFile>
#include <QTest>

// The game crashes on the configure that tells its fullscreen window the
// smaller size, before it has answered it. Nothing is left in flight, the next
// game is told the same, and once the grace a departed program's negotiation
// is kept for has passed (see UpscaleX11Resolution::expireState()), the effect
// keeps no more than it did before the first.
void UpscaleX11IntegrationTest::aGameThatCrashesWhileResizedIsLetGo()
{
    if (crashGame().isEmpty()) {
        QSKIP("needs the crashing glmark2 from tools/prepare-crash-game.py");
    }
    QFile catalogue(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(catalogue.open(QIODevice::Append));
    QVERIFY(catalogue.write("[Application-crashgame]\nName=Crashing game\nExecutable=.*/glmark2\n"
                            "ExecutableMatch=RegularExpression\nMethodX11FullScreen=X11Resize\n")
            > 0);
    catalogue.close();
    KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"))->reparseConfiguration();
    configure(false);
    const QString before = crashRecords(status());
    const QString told = QStringLiteral("UPSCALE_TEST_CRASH resize 1920x1080");
    for (int run = 0; run < 2; ++run) {
        QCOMPARE(crashAt(QStringLiteral("glmark2"), QStringLiteral("resize")), told);
        QTRY_VERIFY2(status().contains(QStringLiteral("x11Settled: true")), qPrintable(status()));
        QTRY_COMPARE_WITH_TIMEOUT(crashRecords(status()), before, 15000);
    }
}
