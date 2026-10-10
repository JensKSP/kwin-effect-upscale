/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the effect keeps of a process the X11 session proxy answered with a
// smaller screen: the screen, the entry that answered, and whether the program
// was a game, which is what All games asks of a window of that process later.

#include "runtime.h"

#include <QTest>

using namespace KWin;

class ServedTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keepsWhetherTheProgramWasAGame();
};

void ServedTest::keepsWhetherTheProgramWasAGame()
{
    // An entry answered a program that is no game. Switched off later, the
    // entry no longer claims its window, and All games must not take it over.
    upscaleRecordServed(1001, QSize(2560, 1440), QStringLiteral("tool"), false);
    QVERIFY(upscaleServed(1001));
    QVERIFY(!upscaleServedGame(1001));
    QCOMPARE(upscaleServedScreen(1001), QSize(2560, 1440));

    // The global profile answers games alone.
    upscaleRecordServed(1002, QSize(2560, 1440), QString(), true);
    QVERIFY(upscaleServedGame(1002));

    // A process of the same Wine prefix shown the game's screen is the game's,
    // and one shown a program's that is no game is not.
    upscaleRecordShown(1002, 1003);
    QVERIFY(upscaleServedGame(1003));
    upscaleRecordShown(1001, 1004);
    QVERIFY(upscaleServed(1004));
    QVERIFY(!upscaleServedGame(1004));

    // A process nothing answered is none of these.
    QVERIFY(!upscaleServed(1005));
    QVERIFY(!upscaleServedGame(1005));
    QVERIFY(!upscaleServedGame(0));
}

QTEST_GUILESS_MAIN(ServedTest)

#include "served_test.moc"
