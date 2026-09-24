/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"

#include <QSaveFile>
#include <QTest>

void UpscaleX11IntegrationTest::keepsEmulatedPointerCoverage_data()
{
    QTest::addColumn<int>("output");
    QTest::newRow("primary") << 0;
    QTest::newRow("secondary") << 1;
}

void UpscaleX11IntegrationTest::keepsEmulatedPointerCoverage()
{
    QFETCH(int, output);
    const QPoint origin(output * 3840, 0);
    const QRect native(origin, QSize(3840, 2160));
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), native, false));
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(origin, QSize(1920, 1080)), false));
    QVERIFY(target.mode(QSize(1920, 1080)));
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_COMPARE(target.geometry(), QRect(origin, QSize(1920, 1080)));
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by Xwayland's emulated mode")), qPrintable(status()));

    target.inputShape(QRect(0, 0, 1920, 1080));
    QTRY_VERIFY(status().contains(QStringLiteral("inputRegion: QRegion(0,0 1920x1080)")));
    // Xwayland already scales coordinates. Extending input coverage must not
    // scale them again, and clicks in the extended area must never reach below.
    // Enter and motion are separate Wayland events. Xwayland 24.1.6 does not
    // apply viewport scaling to its initial enter; measure a subsequent motion.
    movePointer(origin + QPoint(100, 100));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    movePointer(origin + QPoint(120, 120));
    QTRY_COMPARE(target.lastMotion(), QPoint(60, 60));
    movePointer(origin + QPoint(2880, 1620));
    QTRY_COMPARE(target.lastMotion(), QPoint(1440, 810));
    const auto click = [origin]() {
        QSaveFile request(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-click"));
        QVERIFY(request.open(QIODevice::WriteOnly));
        QVERIFY(request.write(QByteArray::number(origin.x() + 2880) + " 1620") > 0);
        QVERIFY(request.commit());
    };
    click();
    QTRY_COMPARE(target.presses(), 1);
    QCOMPARE(target.lastPress(), QPoint(1440, 810));
    QCOMPARE(below.presses(), 0);

    // A smaller intentional input shape is not the complete drawable. Stop
    // claiming clicks as soon as it replaces the shape which needed repair.
    target.inputShape(QRect(0, 0, 960, 540));
    QTRY_VERIFY(status().contains(QStringLiteral("inputRegion: QRegion(0,0 960x540)")));
    click();
    QTRY_COMPARE(below.presses(), 1);
    QCOMPARE(target.presses(), 1);
}
