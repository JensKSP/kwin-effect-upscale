/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

#include "effect/globals.h"

#include <QLocale>
#include <QTest>

using namespace KWin;

// The block a player keeps on screen while playing. It has its own file
// because it has its own contract: every figure in it is read at a glance, at
// a distance, by someone who is not going to look anything up, and what it may
// leave out matters as much as what it shows.
class UpscaleHeadsUpTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void figuresFollowTheReadersLanguage();
    void distinguishesBypassFromNative();
    void keepsUnusualDimensions();
    void separatesTheGameFromTheScreen();
    void showsTheRateItJustMeasured();
    void keepsItsColumnsStill();

private:
    static UpscaleSnapshot scaling();
};

UpscaleSnapshot UpscaleHeadsUpTest::scaling()
{
    // Only what this block reads. A fixture carrying the whole state would
    // suggest the display depends on fields it never looks at.
    UpscaleSnapshot snapshot;
    snapshot.scaling = true;
    snapshot.sharpening = 0.5;
    snapshot.supplied = QSize(1280, 720);
    snapshot.destination = QSize(3840, 2160);
    snapshot.windowSystem = UpscaleWindowSystem::Wayland;
    snapshot.presentedRate = 59.94;
    snapshot.presentedLow = 41.2;
    snapshot.clientUpdates = 59.8;
    return snapshot;
}

void UpscaleHeadsUpTest::initTestCase()
{
    // The source language of this project, so the rest of these cases state
    // one formatting and mean it. What a German reader sees is its own case.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void UpscaleHeadsUpTest::figuresFollowTheReadersLanguage()
{
    // A figure is read by whoever is playing, in their language: a German
    // session writes a comma where a US English one writes a point, and
    // "1.053 ms/f" means a thousand times too much to somebody reading it as
    // German. The machine-readable metrics line is the opposite case and is
    // deliberately not localised.
    UpscaleSnapshot snapshot = scaling();
    snapshot.presentedRate = 237.1;
    snapshot.clientUpdates = 949.4;
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const QString german = upscaleHeadsUp(snapshot);
    QLocale::setDefault(previous);
    QVERIFY2(german.contains(QStringLiteral("237,1 FPS")), qPrintable(german));
    QVERIFY2(german.contains(QStringLiteral("1,053 ms/f")), qPrintable(german));

    const QString english = upscaleHeadsUp(snapshot);
    QVERIFY2(english.contains(QStringLiteral("237.1 FPS")), qPrintable(english));
    QVERIFY2(english.contains(QStringLiteral("1.053 ms/f")), qPrintable(english));

    // The width is the same either way, which is what the fixed columns are
    // for: a language that moves the separator does not add one.
    QCOMPARE(german.split(QLatin1Char('\n')).at(0).size(),
             english.split(QLatin1Char('\n')).at(0).size());
}

void UpscaleHeadsUpTest::distinguishesBypassFromNative()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.scaling = false;
    snapshot.refusal = UpscaleRefusal::TransformedPass;
    const QString bypass = upscaleHeadsUp(snapshot);
    QVERIFY2(bypass.contains(QStringLiteral("FSR off")), qPrintable(bypass));
    QVERIFY(bypass.contains(QStringLiteral("720p → 4K")));
    QVERIFY(!bypass.contains(QStringLiteral("native")));

    snapshot.supplied = snapshot.destination;
    QVERIFY(upscaleHeadsUp(snapshot).contains(QStringLiteral("4K native")));
    snapshot.supplied = QSize();
    const QString unknown = upscaleHeadsUp(snapshot);
    QVERIFY(!unknown.contains(QStringLiteral("native")));
    QVERIFY(unknown.contains(QStringLiteral("unknown → 4K")));
}

void UpscaleHeadsUpTest::keepsUnusualDimensions()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.supplied = QSize(1720, 720);
    snapshot.destination = QSize(5160, 2160);
    const QString ultrawide = upscaleHeadsUp(snapshot);
    QVERIFY2(ultrawide.contains(QStringLiteral("1720 × 720 → 5160 × 2160")), qPrintable(ultrawide));
    QVERIFY(!ultrawide.contains(QStringLiteral("4K")));
    QVERIFY(ultrawide.contains(QStringLiteral("33%")));

    snapshot.supplied = QSize(2560, 1440);
    snapshot.destination = QSize(3840, 2160);
    const QString quality = upscaleHeadsUp(snapshot);
    QVERIFY(quality.contains(QStringLiteral("1440p → 4K")));
    QVERIFY(quality.contains(QStringLiteral("67%")));
    snapshot.supplied = QSize(1920, 1080);
    QVERIFY(upscaleHeadsUp(snapshot).contains(QStringLiteral("1080p → 4K")));
}

void UpscaleHeadsUpTest::separatesTheGameFromTheScreen()
{
    // Measured on 2026-09-19 at 3840 × 2160 on a 240 Hz screen: SuperTuxKart
    // presented 237/s at every preset while committing 493, 833 and 949
    // buffers a second. A block built from the presented rate alone therefore
    // reads identically whatever the effect does, which is the one thing this
    // display must never do.
    UpscaleSnapshot snapshot = scaling();
    snapshot.presentedRate = 237.1;
    snapshot.clientUpdates = 949.4;
    const QString block = upscaleHeadsUp(snapshot);
    QVERIFY2(block.contains(QStringLiteral("237.1 FPS")), qPrintable(block));
    QVERIFY2(block.contains(QStringLiteral("1.053 ms/f")), qPrintable(block));
    // The screen's own frame time is the reciprocal of a rate that has reached
    // the refresh, so it would read the same at every resolution. It is gone.
    QVERIFY2(!block.contains(QStringLiteral("4.2 ms")), qPrintable(block));

    // A rate nobody has measured stays a dash, exactly as the screen's does.
    snapshot.clientUpdates = -1;
    const QString unmeasured = upscaleHeadsUp(snapshot);
    QVERIFY2(unmeasured.contains(QStringLiteral("— ms/f")), qPrintable(unmeasured));
    QVERIFY2(!unmeasured.contains(QStringLiteral("1.053 ms/f")), qPrintable(unmeasured));
}

void UpscaleHeadsUpTest::showsTheRateItJustMeasured()
{
    // The rate over every frame held reaches back 4.3 seconds at 240 Hz, which
    // is why the block looked frozen while a game was plainly struggling. The
    // recent one is what it shows; the long one is what the tail figures need.
    UpscaleSnapshot snapshot = scaling();
    snapshot.presentedRate = 237.1;
    snapshot.presentedRecent = 118.4;
    const QString block = upscaleHeadsUp(snapshot);
    QVERIFY2(block.contains(QStringLiteral("118.4 FPS")), qPrintable(block));
    QVERIFY2(!block.contains(QStringLiteral("237.1 FPS")), qPrintable(block));

    // A snapshot nothing measured a recent rate for still shows what it has,
    // because the settings page builds one without the statistics behind it.
    snapshot.presentedRecent = -1;
    QVERIFY2(upscaleHeadsUp(snapshot).contains(QStringLiteral("237.1 FPS")),
             qPrintable(upscaleHeadsUp(snapshot)));
}

void UpscaleHeadsUpTest::keepsItsColumnsStill()
{
    // A block whose text shifts as its numbers change is unreadable at the
    // distance this is read from, and the numbers change every second.
    UpscaleSnapshot snapshot = scaling();
    snapshot.presentedRate = 9.5;
    const QStringList narrow = upscaleHeadsUp(snapshot).split(QLatin1Char('\n'));
    snapshot.presentedRate = 237.1;
    const QStringList wide = upscaleHeadsUp(snapshot).split(QLatin1Char('\n'));
    QCOMPARE(narrow.at(0).size(), wide.at(0).size());
    QVERIFY2(narrow.at(0).contains(QStringLiteral("9.500 FPS")), qPrintable(narrow.at(0)));

    // The window system ends the second line, so it stays at the right edge
    // whatever the picture beside it says.
    QVERIFY2(wide.at(1).endsWith(QStringLiteral("Wayland")), qPrintable(wide.at(1)));
    QVERIFY2(wide.at(1).size() >= wide.at(0).size(), qPrintable(wide.join(QLatin1Char('|'))));

    // Nothing presents ten thousand frames a second, and a frame slower than
    // a second is reported as a second rather than widening the block.
    snapshot.presentedRate = 99999;
    // Grouped, because US English groups a thousand - and still five columns,
    // which is the point: the separator replaces the padding rather than
    // widening the field.
    QVERIFY2(upscaleHeadsUp(snapshot).contains(QStringLiteral("9,999 FPS")),
             qPrintable(upscaleHeadsUp(snapshot)));
    snapshot.presentedRate = 0.0001;
    QVERIFY2(upscaleHeadsUp(snapshot).contains(QStringLiteral("0.999 FPS")),
             qPrintable(upscaleHeadsUp(snapshot)));
}

QTEST_GUILESS_MAIN(UpscaleHeadsUpTest)

#include "headsup_test.moc"
