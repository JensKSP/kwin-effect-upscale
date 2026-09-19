/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wayland_client.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QSocketNotifier>
#include <QTest>

class UpscaleIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void lifecycle();
    void asksApplicationsForASmallerImage();

private:
    QString status();
    void configure(bool enabled, bool sharpening, int preset = 0);
    void configureColors(bool unsupported);
    void configureDisplay(bool enabled, bool statistics);
    void configureResolution(bool control, bool unlisted, int preset);
    void writeCatalogue(const QString &contents);
    void reconfigure();
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};

QString UpscaleIntegrationTest::status()
{
    const QDBusReply<QString> reply = m_effects.call(QStringLiteral("supportInformation"), QStringLiteral("upscale_test_driver"));
    return reply.isValid() ? reply.value() : reply.error().message();
}

void UpscaleIntegrationTest::configure(bool enabled, bool sharpening, int preset)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Enabled", enabled);
    group.writeEntry("Sharpening", sharpening);
    group.writeEntry("Strength", 50);
    group.writeEntry("Preset", preset);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleIntegrationTest::configureColors(bool unsupported)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale-test"));
    group.writeEntry("UnsupportedColors", unsupported);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleIntegrationTest::configureDisplay(bool enabled, bool statistics)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Osd", enabled);
    group.writeEntry("OsdDetection", true);
    group.writeEntry("OsdSummary", true);
    group.writeEntry("OsdStatistics", statistics);
    group.writeEntry("OsdDeveloper", false);
    group.writeEntry("OsdTimeout", 1);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleIntegrationTest::reconfigure()
{
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleIntegrationTest::configureResolution(bool control, bool unlisted, int preset)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Enabled", true);
    group.writeEntry("ResolutionControl", control);
    group.writeEntry("UnknownApplications", unlisted);
    group.writeEntry("Preset", preset);
    group.sync();
    reconfigure();
}

// The user's own layer of the application list, in the session's private
// configuration directory. The effect reads it when it is reconfigured.
void UpscaleIntegrationTest::writeCatalogue(const QString &contents)
{
    QFile file(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents.toUtf8()), qint64(contents.toUtf8().size()));
    file.close();
    reconfigure();
}

// A program decides the size of the image it renders before it has a window,
// from the display information it was given when it connected. This is the
// whole mechanism: a client that binds the output after the effect is
// configured has to be told a smaller mode, and only that client.
void UpscaleIntegrationTest::asksApplicationsForASmallerImage()
{
    QTRY_VERIFY(m_effects.isValid());
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid());

    // Nothing is said while the request is switched off, whatever resolution
    // is preferred. The screen is 128 × 128, so its own mode is what arrives.
    configureResolution(false, true, 3);
    {
        WaylandClient untouched;
        QVERIFY(untouched.initialize());
        QCOMPARE(untouched.advertisedMode(), QSize(128, 128));
    }

    // Quality is two thirds of the destination, which on this screen is 85.
    configureResolution(true, true, 3);
    {
        WaylandClient asked;
        QVERIFY(asked.initialize());
        QCOMPARE(asked.advertisedMode(), QSize(85, 85));
        // The refresh rate stays the screen's own: only the size is in
        // question, and frame pacing is not this effect's to change.
        // Nothing asks this client for a scale, because an unscaled screen
        // offers a scale-driven client no whole step below one.
        QCOMPARE(asked.advertisedScale(), 1);
    }

    // A client that was already connected cannot be told anything: it read the
    // display information once, before this. Turning the request off has to
    // hand the screen's own mode back to the clients that were told otherwise,
    // so that the next thing to inspect these resources sees KWin's answer.
    {
        WaylandClient restored;
        QVERIFY(restored.initialize());
        QCOMPARE(restored.advertisedMode(), QSize(85, 85));
        configureResolution(false, true, 3);
        QVERIFY(restored.roundtrip());
        QCOMPARE(restored.advertisedMode(), QSize(128, 128));
        QCOMPARE(restored.advertisedScale(), 1);
    }

    // Unlisted applications are off unless the user asks: nothing is known in
    // advance about a program nobody measured.
    configureResolution(true, false, 3);
    {
        WaylandClient unlisted;
        QVERIFY(unlisted.initialize());
        QCOMPARE(unlisted.advertisedMode(), QSize(128, 128));
    }

    // A catalogue entry reaches the same client through its program name,
    // which is the only identity that exists before it has a window.
    writeCatalogue(QStringLiteral("[Application-integrationtest]\n"
                                  "Name=Upscale integration test\n"
                                  "WindowClass=org.kde.upscale.integrationtest\n"
                                  "Program=upscale_integration_test\n"
                                  "Method=AdvertisedMode\n"
                                  "Preset=Performance\n"
                                  "Order=1\n"));
    {
        // The entry's own resolution applies while the global preset is
        // Automatic, so installing the effect is enough for a known game.
        configureResolution(true, false, 0);
        WaylandClient known;
        QVERIFY(known.initialize());
        QCOMPARE(known.advertisedMode(), QSize(64, 64));

        // An explicit global choice wins over the entry's own resolution.
        configureResolution(true, false, 3);
        WaylandClient chosen;
        QVERIFY(chosen.initialize());
        QCOMPARE(chosen.advertisedMode(), QSize(85, 85));

        // What was asked for is reported apart from what arrived: the client
        // is free to ignore the request, and the committed buffer is the only
        // evidence of what it did.
        QSocketNotifier notifier(chosen.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&chosen]() {
            chosen.dispatch();
        });
        QVERIFY(chosen.show(QSize(64, 64)));
        QTRY_VERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test")),
                     qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Supplied input: 64 × 64")), qPrintable(status()));

        // Changing a setting while the effect stays enabled moves what it
        // would ask for, so the resources of a client that was told otherwise
        // get KWin's own answer back. The client will not read them again, but
        // one that binds the output again, and anything that inspects them,
        // would otherwise see a mode this effect no longer asks for.
        configureResolution(true, false, 5);
        QVERIFY(chosen.roundtrip());
        QCOMPARE(chosen.advertisedMode(), QSize(128, 128));
        // What that program was told is still what explains the size it is
        // rendering, so the report of it outlives the resources.
        QVERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test")),
                 qPrintable(status()));
    }

    // A method that says nothing recognizes the application and asks it for
    // nothing, which is what an unmeasured program gets rather than a guess.
    writeCatalogue(QStringLiteral("[Application-integrationtest]\n"
                                  "Name=Upscale integration test\n"
                                  "WindowClass=org.kde.upscale.integrationtest\n"
                                  "Program=upscale_integration_test\n"
                                  "Method=None\n"
                                  "Order=1\n"));
    {
        WaylandClient silent;
        QVERIFY(silent.initialize());
        QCOMPARE(silent.advertisedMode(), QSize(128, 128));
    }

    // Native asks for the size the screen already has, which says nothing and
    // would leave the scaler nothing to enlarge either.
    writeCatalogue(QStringLiteral("[Application-integrationtest]\n"
                                  "Name=Upscale integration test\n"
                                  "WindowClass=org.kde.upscale.integrationtest\n"
                                  "Program=upscale_integration_test\n"
                                  "Method=AdvertisedMode\n"
                                  "Order=1\n"));
    configureResolution(true, false, 1);
    {
        WaylandClient native;
        QVERIFY(native.initialize());
        QCOMPARE(native.advertisedMode(), QSize(128, 128));
    }

    // A scale-driven client is moved only in whole steps of the output's own
    // scale. This screen has no scale above one, so it offers such a client
    // nothing at all, and the effect says nothing rather than asking for a
    // size the client would ignore.
    writeCatalogue(QStringLiteral("[Application-integrationtest]\n"
                                  "Name=Upscale integration test\n"
                                  "WindowClass=org.kde.upscale.integrationtest\n"
                                  "Program=upscale_integration_test\n"
                                  "Method=AdvertisedModeAndScale\n"
                                  "Order=1\n"));
    configureResolution(true, false, 3);
    {
        WaylandClient scaled;
        QVERIFY(scaled.initialize());
        QCOMPARE(scaled.advertisedMode(), QSize(128, 128));
        QCOMPARE(scaled.advertisedScale(), 1);
    }

    writeCatalogue(QString());
    configureResolution(true, false, 0);
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    QCOMPARE(status(), QString());
}

void UpscaleIntegrationTest::lifecycle()
{
    QTRY_VERIFY(m_effects.isValid());
    const QDBusReply<bool> supported = m_effects.call(QStringLiteral("isEffectSupported"), QStringLiteral("upscale"));
    QVERIFY(supported.isValid() && !supported.value());
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid());
    QTRY_VERIFY2(status().contains(QStringLiteral("Inactive: there is no window to scale")), qPrintable(status()));
    {
        WaylandClient client;
        QVERIFY(client.initialize());
        QSocketNotifier notifier(client.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&client]() {
            client.dispatch();
        });
        QVERIFY(client.show(QSize(64, 64)));
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Supplied input: 64 × 64")), qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Destination: 128 × 128")), qPrintable(status()));
        client.commit();
        configure(true, true, 3);
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 50%")), qPrintable(status()));
        QVERIFY(status().contains(QStringLiteral("Select 85 × 85 in the game")));
        configure(false, false);
        QTRY_VERIFY(status().contains(QStringLiteral("Inactive: disabled")));
        configure(true, false);
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        // Each refused buffer has to be reported by the condition that
        // refused it. A single sentence reciting the whole eligibility rule
        // cannot tell a game rendering at native resolution apart from one
        // whose aspect ratio does not match the output.
        const struct
        {
            QSize size;
            QString reason;
        } refusals[] = {
            {QSize(128, 128), QStringLiteral("not smaller than the destination")},
            {QSize(32, 32), QStringLiteral("less than half the destination size")},
            {QSize(64, 80), QStringLiteral("different aspect ratio than the destination")},
        };
        for (const auto &refusal : refusals) {
            QVERIFY(client.show(refusal.size));
            QTRY_VERIFY2(status().contains(refusal.reason), qPrintable(status()));
        }
        // Refused native-size content only needs composition while its notice
        // is visible. Subsequent damage must not reannounce the same window.
        QVERIFY(client.show(QSize(128, 128)));
        QTRY_VERIFY(status().contains(QStringLiteral("not smaller than the destination")));
        configureDisplay(true, false);
        QTRY_VERIFY(status().contains(QStringLiteral("blocksScanout: true")));
        QTRY_VERIFY(status().contains(QStringLiteral("blocksScanout: false")));
        client.commit();
        QTest::qWait(100);
        QVERIFY(status().contains(QStringLiteral("blocksScanout: false")));
        configureDisplay(true, true);
        QTRY_VERIFY(status().contains(QStringLiteral("blocksScanout: true")));
        configureDisplay(false, false);
        QTRY_VERIFY(status().contains(QStringLiteral("blocksScanout: false")));
        // The screen's own frames are measured whether or not the display is
        // drawn, so a report asked for with it switched off carries them. The
        // display is off here and nothing draws it, and the rate still has to
        // arrive rather than the sentence about nothing being presented.
        client.commit();
        QTRY_VERIFY2(status().contains(QStringLiteral("Presented at")), qPrintable(status()));
        configureDisplay(true, true);
        QVERIFY(client.show(QSize(64, 64), false));
        QTRY_VERIFY2(status().contains(QStringLiteral("not fully opaque")), qPrintable(status()));
        QVERIFY(client.show(QSize(64, 64)));
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        client.fullscreen(false);
        QTRY_VERIFY2(status().contains(QStringLiteral("the window is not fullscreen")), qPrintable(status()));
        client.fullscreen(true);
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        {
            WaylandClient second;
            QVERIFY(second.initialize());
            QSocketNotifier secondNotifier(second.descriptor(), QSocketNotifier::Read);
            connect(&secondNotifier, &QSocketNotifier::activated, this, [&second]() {
                second.dispatch();
            });
            QVERIFY(second.show(QSize(64, 64)));
            QTRY_VERIFY2(status().contains(QStringLiteral("more than one fullscreen window is eligible")), qPrintable(status()));
        }
        client.commit();
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        // A render target this effect cannot decode must not leave it
        // "active", because an active effect keeps the output in composition
        // for frames it hands straight back to KWin. Applying settings is the
        // way back, the same as after a graphics resource failure.
        configureColors(true);
        QTRY_VERIFY2(status().contains(QStringLiteral("colour handling is not supported")), qPrintable(status()));
        configureColors(false);
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
        // Reload while a window already exists exercises initial window
        // discovery as well as releasing all resources owned by the effect.
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
        QCOMPARE(status(), QString());
        const QDBusReply<bool> reloaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
        QVERIFY(reloaded.isValid() && reloaded.value());
        client.commit();
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
    }
    QTRY_VERIFY(status().contains(QStringLiteral("Inactive: there is no window to scale")));
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    QCOMPARE(status(), QString());
}

QTEST_GUILESS_MAIN(UpscaleIntegrationTest)

#include "integration_test.moc"
