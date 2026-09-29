/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What becomes of an advertisement when one of its two ends goes away, a
// client an advertisement cannot be told in full, and a surface scale asked
// while the output's own scale changes. Members of the same test class. The
// cases about outputs and scale need a session with two outputs at scale 2,
// which sessions.cmake starts for them, and skip in any other; the last one
// runs in both sessions.

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

// Gives every output @p scale, as System Settings would, without reconfiguring
// the effect.
void UpscaleIntegrationTest::setOutputScale(double scale)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale-test"));
    group.writeEntry("OutputScale", scale);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    group.deleteEntry("OutputScale");
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
        // Each output's xdg_output, told apart by the position KWin sends on
        // it, reports the logical size of the mode told for that output: 85
        // pixels at scale 2, where the output's own is 64.
        QTRY_VERIFY((game.roundtrip(), game.advertisedLogicalSize(0) == QSize(43, 43) && game.advertisedLogicalSize(1) == QSize(43, 43)));
        disableOutput(1);
        QTRY_VERIFY(game.roundtrip() && !game.offered(1));
        QVERIFY(game.offered(0));
        configureResolution(false, false, Stored::Quality);
        QVERIFY(game.roundtrip());
        QCOMPARE(game.advertisedMode(0), QSize(128, 128));
        QCOMPARE(game.advertisedScale(0), 2);
        QCOMPARE(game.advertisedLogicalSize(0), QSize(64, 64));
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
        // Quality wishes 85 × 85, which a whole step cannot say; the report
        // names the step as the nearest, not as a wish for the next start.
        QSocketNotifier notifier(current.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&current]() {
            current.dispatch();
        });
        QVERIFY(current.show(QSize(64, 64)));
        QTRY_VERIFY2(status().contains(QStringLiteral("64 × 64 requested from Upscale integration test as its screen mode, the nearest to 85 × 85 it can be told")),
                     qPrintable(status()));
    }
    WaylandClient old(1);
    QVERIFY(old.initialize());
    QCOMPARE(old.advertisedMode(), QSize(128, 128));
}

// KWin gives a window its output's scale again whenever that scale changes. A
// surface scale asked of it either stands through the change, or ends while
// the window has not yet taken the output's new size and so does not cover
// it, which depends on when the effect looks. Either way the window has to be
// asked of the new scale, and given the new scale back when the request ends,
// not the one it had when first asked: given the old one, it drew a smaller
// buffer than it was asked for, and the effect took that as reached. Two
// thirds of an output at scale 1 is a surface scale of two thirds, at scale 2
// four thirds.
void UpscaleIntegrationTest::aSurfaceScaleFollowsTheOutputScale()
{
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    const int before = game.advertisedScale();
    const int after = before == 1 ? 2 : 1;
    const auto restore = qScopeGuard([this, before]() {
        setOutputScale(before);
        stopAdvertising();
    });
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    // The entry names no method, so the slot follows the global profile's,
    // Auto, which switching the effect's requests off below turns off.
    writeCatalogue(integrationEntry(QStringLiteral("MinimumPixels=0\n")));
    configureResolution(true, false, Stored::Quality);
    configureDisplay(false, false);
    QVERIFY(game.show(QSize(128, 128)));
    QTRY_VERIFY2(game.preferredScale() == 80 * before, qPrintable(QString::number(game.preferredScale()) + QLatin1Char('\n') + status()));
    // Answered, so that the request stands for as long as the case needs it.
    QVERIFY(game.show(QSize(85, 85)));
    QVERIFY(game.presentFrames(5));
    setOutputScale(after);
    // At full size again, as a game given back its scale would draw.
    QVERIFY(game.show(QSize(128, 128)));
    QTRY_VERIFY2(game.preferredScale() == 80 * after, qPrintable(QString::number(game.preferredScale()) + QLatin1Char('\n') + status()));
    configureResolution(false, false, Stored::Quality);
    QTRY_VERIFY2(game.preferredScale() == 120 * after, qPrintable(QString::number(game.preferredScale()) + QLatin1Char('\n') + status()));
}
