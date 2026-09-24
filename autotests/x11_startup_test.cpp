/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"

#include <QTest>

void UpscaleX11IntegrationTest::initialFullscreenMapping_data()
{
    QTest::addColumn<int>("output");
    QTest::newRow("primary") << 0;
    QTest::newRow("secondary") << 1;
}

void UpscaleX11IntegrationTest::initialFullscreenMapping()
{
    QFETCH(int, output);
    configure(true);
    X11Client target(false);
    const QPoint position(output * 3840, 0);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(position, QSize(1024, 768)), false));
    target.fullscreen(true);
    QVERIFY(target.waitForMapping());
    QCOMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
}

void UpscaleX11IntegrationTest::initialWindowedMapping_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("windowed-timeout") << 0;
    QTest::newRow("effect-unloaded") << 1;
    QTest::newRow("effect-disabled") << 2;
    QTest::newRow("unrelated-window") << 3;
}

void UpscaleX11IntegrationTest::initialWindowedMapping()
{
    QFETCH(int, action);
    configure(true);
    X11Client target(false);
    const QByteArray identity = action == 3 ? QByteArrayLiteral("unrelated-x11-test") : QByteArrayLiteral("upscale-x11-test");
    const QRect initial(0, 0, 1024, 768);
    QVERIFY(target.show(identity, initial, false));
    if (action == 1) {
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    } else if (action == 2) {
        configure(false);
    }
    QVERIFY(target.waitForMapping());
    QCOMPARE(target.geometry().size(), initial.size());
    QVERIFY(!target.isFullscreen());
}
