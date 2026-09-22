/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "integration_test.h"

QString UpscaleIntegrationTest::status()
{
    const QDBusReply<QString> reply = m_effects.call(QStringLiteral("supportInformation"), QStringLiteral("upscale_test_driver"));
    return reply.isValid() ? reply.value() : reply.error().message();
}

// The catalogue entry every case here describes the test client by, with
// whatever the case needs appended. It states the program alone - this test's
// own executable, in whatever folder the build put it - which is the only
// identity there is before the client has a window, and so what an
// advertisement is matched by. KWin resolves the same path for its window.
QString integrationEntry(const QString &rest)
{
    return QStringLiteral("[Application-integrationtest]\nName=Upscale integration test\n"
                          "Executable=.*/upscale_integration_test\nExecutableMatch=RegularExpression\n")
        + rest;
}

// An entry naming the window alone, for the cases about a window only.
QString integrationWindow(const QString &rest)
{
    return QStringLiteral("[Application-integrationtest]\nName=Upscale integration test\n"
                          "WindowClass=org.kde.upscale.integrationtest\n")
        + rest;
}

// The global resolution, or none, which leaves it at its default as for a
// person who never chose one. A previous release's keys are removed first, so
// that nothing this test wrote earlier is read through their translation.
static void writeResolution(KConfigGroup &group, std::optional<int> resolution)
{
    for (const char *legacy : {"Enabled", "Preset", "UnknownApplications"}) {
        group.deleteEntry(legacy);
    }
    if (resolution) {
        group.writeEntry("Resolution", *resolution);
    } else {
        group.deleteEntry("Resolution");
    }
}

// The test's client is not in the catalogue, so it is the global profile
// that answers for it. That profile is off by default, which is the whole
// point of the default; a test that wants the client scaled switches it on,
// as a person would.
void UpscaleIntegrationTest::configure(bool unlisted, bool sharpening, std::optional<Stored> resolution)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("UnlistedApplications", unlisted);
    group.writeEntry("Sharpening", sharpening);
    group.writeEntry("Strength", 50);
    writeResolution(group, resolution ? std::optional<int>(int(*resolution)) : std::nullopt);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
}

void UpscaleIntegrationTest::configureColors(bool unsupported)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale-test"));
    group.writeEntry("UnsupportedColors", unsupported);
    group.writeEntry("ColorsOnly", true);
    group.sync();
    const QDBusMessage reply = m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
    group.writeEntry("ColorsOnly", false);
    group.sync();
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

// Asking unlisted applications for a smaller image takes two things: the
// global profile acting on them, and the global profile having a method for
// them. The advertised mode in the fullscreen slot is what the previous
// release's single switch asked for, so that is what "unlisted" means here.
// Not asking is the method Off, which is what an absent key means for the
// global profile.
void UpscaleIntegrationTest::configureResolution(bool asking, bool unlisted, std::optional<Stored> resolution)
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("UnlistedApplications", unlisted);
    if (unlisted && asking) {
        group.writeEntry("MethodWaylandFullScreen", QStringLiteral("AdvertisedMode"));
    } else {
        group.deleteEntry("MethodWaylandFullScreen");
    }
    writeResolution(group, resolution ? std::optional<int>(int(*resolution)) : std::nullopt);
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
void UpscaleIntegrationTest::selectedBorderlessPresentation()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    configureResolution(false, false, {});
    writeCatalogue(QString());
    WaylandClient client;
    QVERIFY(client.initialize(false));
    QSocketNotifier notifier(client.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&client]() {
        client.dispatch();
    });
    QVERIFY(client.show(QSize(64, 64)));
    QTRY_VERIFY2(status().contains(QStringLiteral("the window is not fullscreen")), qPrintable(status()));

    // Asking nothing in any Wayland presentation, so that what is observed is
    // the presentation alone: an unset method is Automatic, which would ask
    // this window for a smaller scale.
    writeCatalogue(integrationWindow(QStringLiteral(
        "MethodWaylandFullScreen=Off\nMethodWaylandBorderless=Off\nMethodWaylandWindowed=Off\n")));
    QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening")), qPrintable(status()));
    client.resize(QSize(96, 96));
    QTRY_VERIFY2(status().contains(QStringLiteral("the window is not fullscreen")), qPrintable(status()));
    client.resize(QSize(128, 128));
    QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening")), qPrintable(status()));
    writeCatalogue(QString());
    QTRY_VERIFY2(status().contains(QStringLiteral("the window is not fullscreen")), qPrintable(status()));
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
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
        // Out of the box only the games in the catalogue are touched. This
        // client is not one of them, so it is left alone - and the status says
        // why, which is what tells being left alone apart from being broken.
        QTRY_VERIFY2(status().contains(QStringLiteral("the application is not in the list")), qPrintable(status()));
        // A person who switches unlisted applications on gets it scaled.
        configure(true, false);
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Supplied input: 64 × 64")), qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Destination: 128 × 128")), qPrintable(status()));
        client.commit();
        configure(true, true, Stored::Quality);
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 50%")), qPrintable(status()));
        QVERIFY(status().contains(QStringLiteral("Select 85 × 85 in the game")));
        configure(false, false);
        QTRY_VERIFY2(status().contains(QStringLiteral("the application is not in the list")), qPrintable(status()));
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
        // Leaving fullscreen while still covering the whole output, undecorated,
        // is a borderless window, and the global profile reaches those when it
        // is switched on, as it is here. Only a window that stops covering the
        // output is refused for not being fullscreen.
        client.fullscreen(false);
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
        client.resize(QSize(96, 96));
        QTRY_VERIFY2(status().contains(QStringLiteral("the window is not fullscreen")), qPrintable(status()));
        client.fullscreen(true);
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        {
            // Switching away from a fullscreen game changes nothing about the
            // window: it stays fullscreen, unminimized, on this desktop and
            // covering its output. Only the activation moves. An effect that
            // does not read it goes on scaling a window nobody can see, goes
            // on holding the output in composition for it, and goes on drawing
            // the display over whatever was raised in front of it. Giving the
            // scanout requirement back is what says it stepped out of the way.
            WaylandClient other;
            // An ordinary window raised in front of the game: never fullscreen,
            // and smaller than the output, as such a window is. It is sized
            // before its first commit so that it is never, even for one frame,
            // an undecorated window covering the output - which is a borderless
            // window, and with unlisted applications switched on, as here, a
            // candidate of its own rather than something in the way. Sizing it
            // afterwards raced the effect selecting it on a loaded machine.
            QVERIFY(other.initialize(false));
            QSocketNotifier otherNotifier(other.descriptor(), QSocketNotifier::Read);
            connect(&otherNotifier, &QSocketNotifier::activated, this, [&other]() {
                other.dispatch();
            });
            other.resize(QSize(96, 96));
            QVERIFY(other.show(QSize(64, 64)));
            QTRY_VERIFY2(status().contains(QStringLiteral("blocksScanout: false")), qPrintable(status()));
        }
        client.commit();
        QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
        QTRY_VERIFY2(status().contains(QStringLiteral("blocksScanout: true")), qPrintable(status()));
        {
            // A second fullscreen window on the same output is no longer a
            // reason to give up on both. Activation names the one on screen,
            // by the same rule that retires the window switched away from, so
            // the effect follows it instead of refusing to choose.
            WaylandClient second;
            QVERIFY(second.initialize());
            QSocketNotifier secondNotifier(second.descriptor(), QSocketNotifier::Read);
            connect(&secondNotifier, &QSocketNotifier::activated, this, [&second]() {
                second.dispatch();
            });
            QVERIFY(second.show(QSize(64, 64)));
            QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening 0%")), qPrintable(status()));
            QVERIFY2(!status().contains(QStringLiteral("more than one fullscreen window is eligible")), qPrintable(status()));
        }
        client.commit();
        QTRY_VERIFY(status().contains(QStringLiteral("FSR 1, sharpening 0%")));
        // A render target this effect cannot decode must not leave it
        // "active", because an active effect keeps the output in composition
        // for frames it hands straight back to KWin. A colour change on the
        // output must recover without reconfiguring the effect.
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

void UpscaleIntegrationTest::outputPixelPolicy()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    configureResolution(true, false, Stored::Performance);
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("MinimumPixels", 128 * 128);
    group.sync();
    const QString rule = QStringLiteral("[Application-test]\nExecutable=.*/upscale_integration_test\n"
                                        "ExecutableMatch=RegularExpression\nMethodWaylandFullScreen=AdvertisedMode\n");
    writeCatalogue(rule); // Inherit the global threshold, including equality.
    WaylandClient client;
    QVERIFY(client.initialize());
    QCOMPARE(client.advertisedMode(), QSize(128, 128));
    QSocketNotifier notifier(client.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&client]() {
        client.dispatch();
    });
    QVERIFY(client.show(QSize(64, 64)));
    QTRY_VERIFY2(status().contains(QStringLiteral("at or below the configured minimum")), qPrintable(status()));

    writeCatalogue(rule + QStringLiteral("MinimumPixels=16383\n"));
    QTRY_VERIFY2(status().contains(QStringLiteral("FSR 1, sharpening")), qPrintable(status()));
    {
        WaylandClient above;
        QVERIFY(above.initialize());
        QCOMPARE(above.advertisedMode(), QSize(64, 64));
    }
    writeCatalogue(rule + QStringLiteral("Resolution=Native\nMinimumPixels=0\n"));
    QTRY_VERIFY2(status().contains(QStringLiteral("rule selects Native")), qPrintable(status()));
    {
        WaylandClient native;
        QVERIFY(native.initialize());
        QCOMPARE(native.advertisedMode(), QSize(128, 128));
    }
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

// The settings page asks the effect which open windows an entry would match,
// with the entry in the file's own terms. The answer comes from KWin's view of
// the client: its program is this test's own executable, resolved from the
// connection.
void UpscaleIntegrationTest::answersWhichWindowsAnEntryMatches()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    WaylandClient client;
    QVERIFY(client.initialize());
    QSocketNotifier notifier(client.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&client]() {
        client.dispatch();
    });
    QVERIFY(client.show(QSize(64, 64)));
    QDBusInterface effect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    const auto matching = [&effect](const QString &executable) {
        const QVariantMap entry{{QStringLiteral("Executable"), executable},
                                {QStringLiteral("ExecutableMatch"), QStringLiteral("RegularExpression")}};
        const QDBusReply<QStringList> reply = effect.call(QStringLiteral("windowsMatching"), entry);
        return reply.isValid() ? reply.value() : QStringList{reply.error().message()};
    };
    QTRY_COMPARE(matching(QStringLiteral(".*/upscale_integration_test")), QStringList{QStringLiteral("Upscale integration test")});
    QCOMPARE(matching(QStringLiteral(".*/another_program")), QStringList());
    // An entry with an unusable pattern matches nothing, as it would in the list.
    QCOMPARE(matching(QStringLiteral(".*")), QStringList());
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

// Auto on Wayland. Nothing is said when the program binds its screen, because
// that cannot be taken back; once the window exists its surface is asked for
// a fractional scale, which can. A client that answers with a smaller buffer
// keeps the request. One that keeps drawing at full size, as Qt and SDL 2 do,
// has it given back after thirty frames, so nothing carries a request the
// client is not acting on.
void UpscaleIntegrationTest::autoAsksTheWindowForAFractionalScale()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    // The entry states its program and no method, so every slot is Auto. It
    // states its own limit too: the screen is 128 pixels square, and a case
    // before this one leaves the global limit at exactly that.
    writeCatalogue(integrationEntry(QStringLiteral("MinimumPixels=0\n")));
    configureResolution(true, false, Stored::Quality);
    const auto pump = [this](WaylandClient &client, QSocketNotifier &notifier) {
        connect(&notifier, &QSocketNotifier::activated, this, [&client]() {
            client.dispatch();
        });
    };
    {
        WaylandClient honouring;
        QVERIFY(honouring.initialize());
        QCOMPARE(honouring.advertisedMode(), QSize(128, 128));
        QSocketNotifier notifier(honouring.descriptor(), QSocketNotifier::Read);
        pump(honouring, notifier);
        QVERIFY(honouring.show(QSize(128, 128)));
        // Quality is two thirds, which the protocol carries in 120ths.
        QTRY_VERIFY2(honouring.preferredScale() == 80,
                     qPrintable(QString::number(honouring.preferredScale()) + QLatin1Char('\n') + status()));
        // The report names what Auto asked for, not only the resolution wanted.
        QTRY_VERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test as its surface scale")),
                     qPrintable(status()));
        QVERIFY(honouring.show(QSize(85, 85)));
        for (int frame = 0; frame < 40; ++frame) {
            honouring.commit();
            QTest::qWait(10);
        }
        QCOMPARE(honouring.preferredScale(), 80);
        // A window that stops presenting full screen, and no longer covers its
        // output either, is no longer asked, and gets its own scale back rather
        // than rendering small in a window. Out of fullscreen alone it would
        // still be a borderless window over its output, which Auto also asks.
        honouring.fullscreen(false);
        honouring.resize(QSize(64, 64));
        QTRY_VERIFY2_WITH_TIMEOUT((honouring.commit(), honouring.preferredScale() == 120),
                                  qPrintable(QString::number(honouring.preferredScale()) + QLatin1Char('\n') + status()),
                                  10000);
    }
    {
        WaylandClient ignoring;
        QVERIFY(ignoring.initialize());
        QSocketNotifier notifier(ignoring.descriptor(), QSocketNotifier::Read);
        pump(ignoring, notifier);
        QVERIFY(ignoring.show(QSize(128, 128)));
        QTRY_COMPARE(ignoring.preferredScale(), 80);
        QTRY_VERIFY_WITH_TIMEOUT((ignoring.commit(), ignoring.preferredScale() == 120), 10000);
        // And it is not asked the same question again thirty frames later.
        for (int frame = 0; frame < 60; ++frame) {
            ignoring.commit();
            QTest::qWait(10);
        }
        QCOMPARE(ignoring.preferredScale(), 120);
    }
    writeCatalogue(QString());
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

QTEST_GUILESS_MAIN(UpscaleIntegrationTest)
