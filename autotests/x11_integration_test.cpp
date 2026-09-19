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
#include <QTest>

class UpscaleX11IntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void lifecycle_data();
    void lifecycle();
    void refusesMissingEmulation();
    void expiresDepartedClientRefusal();
    void refusesUnavailableMode();
    void respectsPrimaryOutputRestriction();
    void retriesADroppedResizeOnce();
    void independentOutputRules();
    void repeatedFullscreenTransitions();

private:
    QString status();
    void configure(bool enabled, int preset = 5);
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
        QTRY_VERIFY(target.isFullscreen());
    }
    QTRY_COMPARE(target.geometry(), QRect(position, native));
    configure(true);
    QTRY_COMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
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
    QTRY_VERIFY(other.isFullscreen());
    QTRY_COMPARE(other.geometry(), otherGeometry);
    target.resize(QSize(1600, 900));
    QTest::qWait(100);
    QCOMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(true, 3);
    QTRY_COMPARE(target.geometry(), QRect(position, QSize(2560, 1440)));
    QTest::qWait(3500);
    QCOMPARE(target.geometry(), QRect(position, QSize(2560, 1440)));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(false);
    QTRY_COMPARE(target.geometry(), QRect(position, native));
    configure(true);
    QTRY_COMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    QTRY_COMPARE(target.geometry(), QRect(position, native));
    QCOMPARE(other.geometry(), otherGeometry);
    configure(false);
    const QDBusReply<bool> reloaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reloaded.isValid() && reloaded.value());
    QTest::qWait(100);
    QCOMPARE(target.geometry(), QRect(position, native));
}

void UpscaleX11IntegrationTest::refusesMissingEmulation()
{
    X11Client target(false);
    const QRect native(0, 0, 3840, 2160);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
    QTRY_COMPARE(target.geometry(), native);
    configure(true);
    QTRY_COMPARE(target.geometry().size(), QSize(1920, 1080));
    QTRY_VERIFY_WITH_TIMEOUT(status().contains(QStringLiteral("did not supply the requested fullscreen buffer")), 9000);
    QTRY_COMPARE(target.geometry(), native);
    QVERIFY(!status().contains(QStringLiteral("as its X11 window size")));
    // The refusal must persist after restoration, instead of retrying every
    // damage or geometry notification and trapping the application in a loop.
    target.resize(QSize(1600, 900));
    QTest::qWait(500);
    QCOMPARE(target.geometry(), native);
}

void UpscaleX11IntegrationTest::expiresDepartedClientRefusal()
{
    const QRect native(0, 0, 3840, 2160);
    {
        X11Client target(false);
        QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
        QTRY_VERIFY(target.isFullscreen());
        configure(true);
        QTRY_VERIFY_WITH_TIMEOUT(status().contains(QStringLiteral("did not supply the requested fullscreen buffer")), 9000);
    }
    // A prompt replacement must retain refusal, otherwise an uncooperative
    // client could evade the retry bound by recreating its XID.
    {
        X11Client replacement;
        QVERIFY(replacement.show(QByteArrayLiteral("upscale-x11-test"), native));
        QTRY_VERIFY(replacement.isFullscreen());
        QTRY_VERIFY(status().contains(QStringLiteral("did not supply the requested fullscreen buffer")));
        QCOMPARE(replacement.geometry(), native);
    }
    QTest::qWait(3500);
    // All connections belong to this test process: the same PID/profile/output
    // now represents a later launch, without reconfiguring the plugin.
    X11Client relaunched;
    QVERIFY(relaunched.show(QByteArrayLiteral("upscale-x11-test"), native));
    QTRY_VERIFY(relaunched.isFullscreen());
    QTRY_COMPARE(relaunched.geometry().size(), QSize(1920, 1080));
    QVERIFY2(!status().contains(QStringLiteral("did not supply")), qPrintable(status()));
}

void UpscaleX11IntegrationTest::refusesUnavailableMode()
{
    X11Client target;
    const QRect native(0, 0, 3840, 2160);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), native));
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
        QTRY_VERIFY(target.isFullscreen());
        QTRY_VERIFY2(status().contains(QStringLiteral("as its X11 window size")), qPrintable(status()));
        QTRY_VERIFY2(status().contains(QStringLiteral("QSize(1920, 1080) QSizeF(3840, 2160)")), qPrintable(status()));
        QTRY_VERIFY2(status().contains(QStringLiteral("true true QRectF(0,0 3840x2160)")), qPrintable(status()));
        QTRY_COMPARE(target.geometry().size(), reduced);
        target.fullscreen(false);
        QTRY_VERIFY(!target.isFullscreen());
    }
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
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
