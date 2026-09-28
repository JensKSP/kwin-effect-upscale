/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What becomes of an advertisement when one of its two ends goes away, and a
// client an advertisement cannot be told in full. Members of the same test
// class. The cases about outputs and scale need a session with two outputs at
// scale 2, which sessions.cmake starts for them, and skip in any other.

#include "integration_test.h"

#include <QScopeGuard>

// Switches one output off, or every output back on with -1. The driver does
// it the way System Settings would, without reconfiguring the effect.
void UpscaleIntegrationTest::disableOutput(int index)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale-test"));
    group.writeEntry("DisabledOutput", index);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    group.deleteEntry("DisabledOutput");
    group.sync();
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

// The driver loaded, and the test client's entry asking with the given method.
// The entry states MinimumPixels=0 because an earlier case in the same session
// leaves the global threshold at the size of its screen.
void UpscaleIntegrationTest::startAdvertising(const QString &method)
{
    QTRY_VERIFY(m_effects.isValid());
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=%1\nMinimumPixels=0\nOrder=1\n").arg(method)));
    configureResolution(true, false, Stored::Quality);
}

// Undone on every way out of a case, so that one that fails part way does not
// leave the next one a loaded driver to trip over.
void UpscaleIntegrationTest::stopAdvertising()
{
    writeCatalogue(QString());
    configureResolution(true, false, {});
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

// An output that goes away while a program is recorded as told a smaller mode
// on it. Giving the mode back afterwards must still reach the output that is
// left, and once the output is back, a program starting then is told on it
// again: the effect must neither hold on to what went away nor stop watching.
void UpscaleIntegrationTest::anOutputThatGoesAwayWhileAdvertised()
{
    if (qEnvironmentVariableIntValue("UPSCALE_TEST_OUTPUT_COUNT") < 2) {
        QSKIP("needs a second output to take away");
    }
    const auto restore = qScopeGuard([this]() {
        disableOutput(-1);
        stopAdvertising();
    });
    startAdvertising(QStringLiteral("AdvertisedMode"));
    if (QTest::currentTestFailed()) {
        return;
    }
    {
        WaylandClient game;
        QVERIFY(game.initialize());
        QCOMPARE(game.advertisedMode(0), QSize(85, 85));
        QCOMPARE(game.advertisedMode(1), QSize(85, 85));
        disableOutput(1);
        QTRY_VERIFY(game.roundtrip() && !game.offered(1));
        QVERIFY(game.offered(0));
        configureResolution(false, false, Stored::Quality);
        QVERIFY(game.roundtrip());
        QCOMPARE(game.advertisedMode(0), QSize(128, 128));
        QCOMPARE(game.advertisedScale(0), 2);
    }
    configureResolution(true, false, Stored::Quality);
    disableOutput(-1);
    WaylandClient next;
    QVERIFY(next.initialize());
    QCOMPARE(next.advertisedMode(0), QSize(85, 85));
    QCOMPARE(next.advertisedMode(1), QSize(85, 85));
}

// A program that exits while it is recorded as told a smaller mode. Giving the
// mode back afterwards must pass over it and still reach the programs that are
// left, and the next program to start must be told as before.
void UpscaleIntegrationTest::aProgramThatExitsBeforeRestoration()
{
    const auto restore = qScopeGuard([this]() {
        stopAdvertising();
    });
    startAdvertising(QStringLiteral("AdvertisedMode"));
    if (QTest::currentTestFailed()) {
        return;
    }
    WaylandClient survivor;
    QVERIFY(survivor.initialize());
    QCOMPARE(survivor.advertisedMode(), QSize(85, 85));
    const QString requested = QStringLiteral("85 × 85 requested from Upscale integration test");
    {
        WaylandClient game;
        QVERIFY(game.initialize());
        QCOMPARE(game.advertisedMode(), QSize(85, 85));
        QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
            game.dispatch();
        });
        QVERIFY(game.show(QSize(85, 85)));
        QTRY_VERIFY2(status().contains(requested), qPrintable(status()));
    }
    // KWin closes the window when the connection ends, so once the report no
    // longer names it, the program is gone for the effect as well.
    QTRY_VERIFY2(!status().contains(requested), qPrintable(status()));
    configureResolution(false, false, Stored::Quality);
    QVERIFY(survivor.roundtrip());
    QCOMPARE(survivor.advertisedMode(), QSize(128, 128));
    configureResolution(true, false, Stored::Quality);
    WaylandClient next;
    QVERIFY(next.initialize());
    QCOMPARE(next.advertisedMode(), QSize(85, 85));
}

// A program that binds the first version of wl_output has no scale event, so
// a method that names a scale cannot be told to it in full. It is told
// nothing, rather than a mode its scale would contradict, while a program that
// binds a later version hears the same method whole. Only a scaled output
// offers such a method a step at all.
void UpscaleIntegrationTest::anOutputVersionWithoutScaleIsLeftAlone()
{
    if (qEnvironmentVariableIntValue("UPSCALE_TEST_OUTPUT_SCALE") < 2) {
        QSKIP("needs an output at a scale above one");
    }
    const auto restore = qScopeGuard([this]() {
        stopAdvertising();
    });
    startAdvertising(QStringLiteral("AdvertisedModeAndScale"));
    if (QTest::currentTestFailed()) {
        return;
    }
    {
        // At scale 2 the one step down is scale 1, half of the 128 pixels.
        WaylandClient current;
        QVERIFY(current.initialize());
        QCOMPARE(current.advertisedMode(), QSize(64, 64));
        QCOMPARE(current.advertisedScale(), 1);
    }
    WaylandClient old(1);
    QVERIFY(old.initialize());
    QCOMPARE(old.advertisedMode(), QSize(128, 128));
}
