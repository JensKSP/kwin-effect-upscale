/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QSaveFile>
#include <QTest>

class UpscaleX11IntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void lifecycle_data();
    void lifecycle();
    void presentsWithoutEmulation();
    void expiresDepartedClientRefusal();
    void refusesUnavailableMode();
    void respectsPrimaryOutputRestriction();
    void retriesADroppedResizeOnce();
    void independentOutputRules();
    void repeatedFullscreenTransitions();

private:
    QString status();
    void configure(bool enabled, int preset = 5);
    void movePointer(const QPoint &position);
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};

QString UpscaleX11IntegrationTest::status()
{
    const QDBusReply<QString> reply = m_effects.call(QStringLiteral("supportInformation"), QStringLiteral("upscale_test_driver"));
    return reply.isValid() ? reply.value() : reply.error().message();
}

void UpscaleX11IntegrationTest::configure(bool enabled, int preset)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Enabled", true);
    group.writeEntry("ResolutionControl", enabled);
    group.writeEntry("Osd", false);
    group.writeEntry("Preset", preset);
    group.writeEntry("MinimumPixels", 1920 * 1080);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleX11IntegrationTest::init()
{
    QTRY_VERIFY(m_effects.isValid());
    QFile catalogue(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(catalogue.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(catalogue.write("[Application-test]\nName=X11 test\nWindowClass=upscale-x11-test\nMethod=X11Resize\nPreset=Performance\n") > 0);
    catalogue.close();
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    configure(false);
}

void UpscaleX11IntegrationTest::cleanup()
{
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

void UpscaleX11IntegrationTest::lifecycle_data()
{
    QTest::addColumn<int>("output");
    QTest::addColumn<bool>("fullscreen");
    QTest::newRow("primary-fullscreen") << 0 << true;
    QTest::newRow("secondary-fullscreen") << 1 << true;
    QTest::newRow("primary-borderless") << 0 << false;
    QTest::newRow("secondary-borderless") << 1 << false;
}

void UpscaleX11IntegrationTest::lifecycle()
{
    QFETCH(int, output);
    QFETCH(bool, fullscreen);
    const QSize native(3840, 2160);
    const QPoint position(output * native.width(), 0);
    X11Client target;
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(position, native), fullscreen));
    if (fullscreen) {
        QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    }
    // Bounded like every other wait here: each one is a round trip through
    // KWin, Xwayland and the client, and an instrumented build makes those
    // slower without making them wrong.
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry(), QRect(position, native), 30000);
    configure(true);
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry(), QRect(position, QSize(1920, 1080)), 30000);
    QTRY_VERIFY2(status().contains(QStringLiteral("Supplied input: 1920 × 1080")), qPrintable(status()));
    QTRY_VERIFY2(status().contains(QStringLiteral("Destination: 3840 × 2160")), qPrintable(status()));
    // Wait beyond negotiation's deadline: a transient small window does not
    // prove an accepted buffer with a full-output presentation.
    QTest::qWait(3500);
    QCOMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
    QVERIFY2(!status().contains(QStringLiteral("did not supply")), qPrintable(status()));
    X11Client other;
    const QRect otherGeometry(QPoint((1 - output) * native.width(), 0), native);
    QVERIFY(other.show(QByteArrayLiteral("unrelated-x11-test"), otherGeometry));
    // The initial unmanaged geometry can already match. Wait for KWin to
    // acknowledge fullscreen before checking the final monitor placement.
    QTRY_VERIFY_WITH_TIMEOUT(other.isFullscreen(), 10000);
    QTRY_COMPARE(other.geometry(), otherGeometry);
    target.resize(QSize(1600, 900));
    QTest::qWait(100);
    QCOMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(true, 3);
    // Changing the preset asks the client for another size, and on KWin 6.6
    // that first request fails its validation: the documented single retry is
    // what recovers it, and the retry costs the whole path - validation 3 s
    // after the request, a restore and reschedule 250 ms later, and validation
    // of the new request 3 s after that. Measured on Ubuntu 26.04 / KWin
    // 6.6.6, where QtTest reported that 8300 ms would have sufficed against
    // the 5000 ms default; KWin 6.3.6 satisfies the request immediately. Wait
    // out the retry rather than the moment 6.3.6 happens to answer in.
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry(), QRect(position, QSize(2560, 1440)), 30000);
    QTest::qWait(3500);
    QCOMPARE(target.geometry(), QRect(position, QSize(2560, 1440)));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(false);
    // Every wait in this case is really a wait for a round trip through KWin,
    // Xwayland and the client, and how long that takes is a property of the
    // machine rather than of this plugin. Observed on master's coverage job,
    // 2026-09-19: this restore missed the 5000 ms default by 50 ms, and
    // QtTest said so - the instrumented build makes each trip slower while
    // measuring nothing about it. A generous bound costs a passing run
    // nothing, because QTRY returns as soon as the condition holds.
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry(), QRect(position, native), 30000);
    configure(true);
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry(), QRect(position, QSize(1920, 1080)), 30000);
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    QTRY_COMPARE(target.geometry(), QRect(position, native));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(false);
    const QDBusReply<bool> reloaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reloaded.isValid() && reloaded.value());
    // A hundred milliseconds was a guess about how long a freshly loaded
    // effect takes to look at the windows it finds, and on a slower machine
    // it is not long enough to distinguish "left alone" from "not yet
    // touched". Observed on Ubuntu 26.04 / KWin 6.6.6 on 2026-09-20, on both
    // architectures: the window comes back at the size that was requested
    // before the unload, although resolution control is off in the
    // configuration this effect has just read. Either something acts while
    // disabled or KWin re-applies Xwayland's emulated mode, and the effect's
    // own status names which - so the status is what a failure reports.
    // What holds here is eventual, not immediate. Observed on the nightly's
    // resolute job, 2026-09-20, with the effect's own status attached to the
    // failure: a second after the reload the window is still at the size the
    // previous request left it, and the effect - which is loaded, with
    // resolution control off - is upscaling that buffer, because upscaling a
    // small buffer is its other job and not something control governs. The
    // window returns to its native size after that, well inside this bound
    // here and on the slower machine alike. Requiring it within a fixed wait
    // was requiring the transient to be over, which is a property of the
    // machine rather than of the effect.
    QTRY_VERIFY2_WITH_TIMEOUT(target.geometry() == QRect(position, native), qPrintable(status()), 30000);
}

void UpscaleX11IntegrationTest::presentsWithoutEmulation()
{
    X11Client target(false);
    const QRect native(0, 0, 3840, 2160);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
    QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    QTRY_COMPARE(target.geometry(), native);
    configure(true);
    // The client accepts the window and supplies the buffer but never asks
    // Xwayland for a mode, which is what Left 4 Dead 2 does. The effect then
    // presents the buffer across the output itself: the window keeps its
    // size past negotiation, the buffer is captured, and status says who is
    // presenting it.
    QTRY_COMPARE(target.geometry().size(), QSize(1920, 1080));
    QTest::qWait(3500);
    QCOMPARE(target.geometry().size(), QSize(1920, 1080));
    QVERIFY2(!status().contains(QStringLiteral("request failed")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Supplied input: 1920 × 1080")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Destination: 3840 × 2160")), qPrintable(status()));
    QTRY_VERIFY2(status().contains(QStringLiteral("captured: upscale-x11-test")), qPrintable(status()));
    // Input follows the picture: a pointer at the middle of the output has to
    // arrive at the middle of the half-size window, not outside it. Nothing
    // is asserted before the compositor has answered, because a stale last
    // motion would pass the wrong assertion.
    movePointer(QPoint(1920, 1080));
    QTRY_COMPARE(target.lastMotion(), QPoint(960, 540));
    // Releasing the window hands KWin's own mapping back at once, without a
    // focus or geometry change to prompt it.
    configure(false);
    QTRY_COMPARE(target.geometry(), native);
    QVERIFY(!status().contains(QStringLiteral("as its X11 window size")));
    movePointer(QPoint(1930, 1090));
    QTRY_COMPARE(target.lastMotion(), QPoint(1930, 1090));
    // Nothing starts a fresh negotiation on its own after the request is
    // released: the client's own resize must not put the effect back to work.
    target.resize(QSize(1600, 900));
    QTest::qWait(500);
    QCOMPARE(target.geometry(), native);
}

void UpscaleX11IntegrationTest::movePointer(const QPoint &position)
{
    // Read and removed by the test driver inside the compositor; see there.
    // It polls, so the request has to appear whole: a truncated file it reads
    // mid-write parses as too few fields, and it removes the file regardless,
    // losing the motion. QSaveFile publishes it by rename instead.
    QSaveFile request(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-pointer"));
    QVERIFY(request.open(QIODevice::WriteOnly));
    QVERIFY(request.write(QByteArray::number(position.x()) + ' ' + QByteArray::number(position.y())) > 0);
    QVERIFY(request.commit());
}

void UpscaleX11IntegrationTest::expiresDepartedClientRefusal()
{
    const QRect native(0, 0, 3840, 2160);
    configure(true);
    // A client that keeps replacing its window is what the negotiation budget
    // exists for, and the budget only works if it survives the replacement:
    // a client could otherwise evade it by recreating its XID. Six windows
    // spend it; the seventh is told so and keeps its own size.
    for (int spent = 0; spent < 6; ++spent) {
        X11Client target;
        QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
        QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
        QTRY_COMPARE(target.geometry().size(), QSize(1920, 1080));
    }
    {
        X11Client refused;
        QVERIFY(refused.show(QByteArrayLiteral("upscale-x11-test"), native));
        QTRY_VERIFY_WITH_TIMEOUT(refused.isFullscreen(), 10000);
        QTRY_VERIFY2(status().contains(QStringLiteral("repeatedly replaced its window")), qPrintable(status()));
        QCOMPARE(refused.geometry(), native);
    }
    QTest::qWait(3500);
    // All connections belong to this test process: the same PID/profile/output
    // now represents a later launch, without reconfiguring the plugin.
    X11Client relaunched;
    QVERIFY(relaunched.show(QByteArrayLiteral("upscale-x11-test"), native));
    QTRY_VERIFY_WITH_TIMEOUT(relaunched.isFullscreen(), 10000);
    QTRY_COMPARE(relaunched.geometry().size(), QSize(1920, 1080));
    QVERIFY2(!status().contains(QStringLiteral("repeatedly replaced")), qPrintable(status()));
}

void UpscaleX11IntegrationTest::refusesUnavailableMode()
{
    X11Client target;
    const QRect native(0, 0, 3840, 2160);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
    QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    QTRY_COMPARE(target.geometry(), native);
    configure(true, 4); // Balanced is 2259 × 1271, absent from this output's modes.
    QTRY_VERIFY2(status().contains(QStringLiteral("requested X11 mode is unavailable")), qPrintable(status()));
    QCOMPARE(target.geometry(), native);
    configure(true);
    QTRY_COMPARE(target.geometry().size(), QSize(1920, 1080));
}

void UpscaleX11IntegrationTest::repeatedFullscreenTransitions()
{
    X11Client target;
    const QSize reduced(1920, 1080);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(QPoint(0, 0), reduced), false));
    QTRY_COMPARE(target.geometry().size(), reduced);
    QVERIFY(target.mode(reduced));
    configure(true);
    // A new emulated-mode window can request fullscreen while its logical
    // frame is still small. The blocked native configure must not prevent
    // KWin from updating that logical frame to cover the output.
    for (int transition = 0; transition < 8; ++transition) {
        target.fullscreen(true);
        QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
        QTRY_VERIFY2(status().contains(QStringLiteral("as its X11 window size")), qPrintable(status()));
        QTRY_VERIFY2(status().contains(QStringLiteral("QSize(1920, 1080) QSizeF(3840, 2160)")), qPrintable(status()));
        QTRY_VERIFY2(status().contains(QStringLiteral("frame QRectF(0,0 3840x2160)")), qPrintable(status()));
        QTRY_COMPARE(target.geometry().size(), reduced);
        target.fullscreen(false);
        QTRY_VERIFY(!target.isFullscreen());
    }
    target.fullscreen(true);
    QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    QTest::qWait(3500);
    QVERIFY2(!status().contains(QStringLiteral("did not supply")), qPrintable(status()));
    QVERIFY2(!status().contains(QStringLiteral("repeatedly replaced")), qPrintable(status()));
    QTRY_VERIFY(status().contains(QStringLiteral("captured: upscale-x11-test")));
}

void UpscaleX11IntegrationTest::respectsPrimaryOutputRestriction()
{
    X11Client target;
    const QRect native(3840, 0, 3840, 2160);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
    // Management and fullscreen are asynchronous. Geometry can already match
    // before either, and briefly changes while KWin chooses the output.
    QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    QTRY_COMPARE(target.geometry(), native);
    const KSharedConfig::Ptr catalogue = KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"));
    KConfigGroup group(catalogue, QStringLiteral("Application-test"));
    group.writeEntry("X11PrimaryOutputOnly", true);
    group.sync();
    configure(true);
    QTRY_VERIFY2(status().contains(QStringLiteral("only supports the primary output")), qPrintable(status()));
    QCOMPARE(target.geometry(), native);
}

void UpscaleX11IntegrationTest::retriesADroppedResizeOnce()
{
    X11Client target(true, 1);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160)));
    QTRY_VERIFY_WITH_TIMEOUT(target.isFullscreen(), 10000);
    QTRY_COMPARE(target.geometry().size(), QSize(3840, 2160));
    configure(true);
    QTest::qWait(7000);
    QCOMPARE(target.geometry().size(), QSize(1920, 1080));
    QVERIFY2(!status().contains(QStringLiteral("request failed")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Destination: 3840 × 2160")), qPrintable(status()));
}

void UpscaleX11IntegrationTest::independentOutputRules()
{
    const KSharedConfig::Ptr catalogue = KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"));
    KConfigGroup second(catalogue, QStringLiteral("Application-second"));
    second.writeEntry("WindowClass", "second-x11-test");
    second.writeEntry("Method", "X11Resize");
    second.writeEntry("Preset", "Native");
    second.sync();
    X11Client first;
    X11Client other;
    QVERIFY(first.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160)));
    QVERIFY(other.show(QByteArrayLiteral("second-x11-test"), QRect(3840, 0, 3840, 2160)));
    QTRY_VERIFY_WITH_TIMEOUT(first.isFullscreen(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(other.isFullscreen(), 10000);
    QTRY_COMPARE(first.geometry().size(), QSize(3840, 2160));
    QTRY_COMPARE(other.geometry().size(), QSize(3840, 2160));
    configure(true); // Global Performance must not override the Native rule.
    QTRY_COMPARE(first.geometry().size(), QSize(1920, 1080));
    QTRY_VERIFY(status().contains(QStringLiteral("captured: upscale-x11-test")));
    QCOMPARE(other.geometry(), QRect(3840, 0, 3840, 2160));

    second.writeEntry("Preset", "Performance");
    second.writeEntry("MinimumPixels", 3840 * 2160); // Equality bypasses.
    second.sync();
    configure(true);
    // Reconfiguring restores every managed window before applying the rules
    // again, so the window this rule does not concern leaves its reduced mode
    // and returns to it. On KWin 6.6 the request that follows that restore
    // fails its validation and the documented single retry is what recovers
    // it: validation runs 3 s after a request, the retry restores and
    // reschedules 250 ms later, and that request is validated 3 s after that.
    // Measured at about 8.1 s on Ubuntu 26.04, against well under 500 ms on
    // 6.3.6. Allow the whole retry path rather than a fixed delay, then give
    // the rule its own delay to resize the other window wrongly, which is
    // what this is watching for.
    QTRY_COMPARE_WITH_TIMEOUT(first.geometry().size(), QSize(1920, 1080), 30000);
    QTest::qWait(500);
    QCOMPARE(other.geometry(), QRect(3840, 0, 3840, 2160));
    QCOMPARE(first.geometry().size(), QSize(1920, 1080));

    second.writeEntry("MinimumPixels", 1920 * 1080);
    second.sync();
    configure(true);
    QTRY_COMPARE(other.geometry(), QRect(3840, 0, 1920, 1080));
    QTRY_COMPARE(first.geometry().size(), QSize(1920, 1080));
    QTRY_VERIFY2(status().contains(QStringLiteral("captured: upscale-x11-test,second-x11-test"))
                     || status().contains(QStringLiteral("captured: second-x11-test,upscale-x11-test")),
                 qPrintable(status()));
    QTest::qWait(3500);
    QCOMPARE(other.geometry(), QRect(3840, 0, 1920, 1080));
    QCOMPARE(first.geometry().size(), QSize(1920, 1080));

    second.writeEntry("Preset", "Native");
    second.sync();
    configure(true);
    QTRY_COMPARE(other.geometry(), QRect(3840, 0, 3840, 2160));
    QTRY_COMPARE(first.geometry().size(), QSize(1920, 1080));
}

QTEST_GUILESS_MAIN(UpscaleX11IntegrationTest)

#include "x11_integration_test.moc"
