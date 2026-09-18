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
#include <QSocketNotifier>
#include <QTest>

class UpscaleIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void lifecycle();

private:
    QString status();
    void configure(bool enabled, bool sharpening, int preset = 0);
    void configureColors(bool unsupported);
    void configureDisplay(bool enabled, bool statistics);
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
