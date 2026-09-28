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
    QTRY_VERIFY(status().contains(QStringLiteral("inputBounds: 0,0,1920,1080")));
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
    QTRY_VERIFY(status().contains(QStringLiteral("inputBounds: 0,0,960,540")));
    click();
    QTRY_COMPARE(below.presses(), 1);
    QCOMPARE(target.presses(), 1);
}

// A fullscreen request can arrive while KWin still remembers the small
// startup window. The effect suppresses KWin's native fullscreen configure,
// but must still propagate the client's resized input shape to its frame.
void UpscaleX11IntegrationTest::refreshesStartupInputShape()
{
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 640, 480), false));
    QVERIFY(target.waitForMapping());
    QVERIFY(target.mode(QSize(1920, 1080)));
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_COMPARE(target.geometry(), QRect(0, 0, 1920, 1080));
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by Xwayland's emulated mode")), qPrintable(status()));

    // Do not set an explicit client shape here: its ShapeNotify would make
    // KWin refresh the frame and conceal the missed update during fullscreen.
    movePointer(logical(QPoint(100, 100)));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    movePointer(logical(QPoint(2880, 1620)));
    QTRY_COMPARE(target.lastMotion(), QPoint(1440, 810));
    const QPoint far = logical(QPoint(2880, 1620));
    QSaveFile click(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-click"));
    QVERIFY(click.open(QIODevice::WriteOnly));
    QVERIFY(click.write(QByteArray::number(far.x()) + ' ' + QByteArray::number(far.y())) > 0);
    QVERIFY(click.commit());
    QTRY_COMPARE(target.presses(), 1);
    QCOMPARE(target.lastPress(), QPoint(1440, 810));
    QCOMPARE(below.presses(), 0);
}

// A program that never asks for a mode is presented by the effect itself
// rather than by Xwayland's emulation, so its surface stays the size it drew
// and does not cover the output. Wine and Proton games are of this kind: they
// take their screen from the prefix and ask X11 for nothing. Pointer coverage
// then has to come from the effect across the whole presented area, including
// the part beyond the surface.
void UpscaleX11IntegrationTest::coversPointerWithoutEmulatedMode()
{
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    // No mode request: this is what separates it from the emulated cases.
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    // An offscreen pass of this window draws it at its own scale, which on an
    // output scaled beyond one differs from the screen's. That is not the pass
    // being presented and must not be reported as the reason this one was not.
    QVERIFY2(!status().contains(QStringLiteral("different scale than the output")), qPrintable(status()));

    movePointer(logical(QPoint(100, 100)));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    // The far corner lies beyond the surface but inside what the effect
    // presents, which is the coverage an emulated mode would have given.
    const QPoint far = logical(QPoint(3600, 2010));
    movePointer(far);
    QTRY_COMPARE(target.lastMotion(), QPoint(1800, 1005));
    QSaveFile click(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-click"));
    QVERIFY(click.open(QIODevice::WriteOnly));
    QVERIFY(click.write(QByteArray::number(far.x()) + ' ' + QByteArray::number(far.y())) > 0);
    QVERIFY(click.commit());
    QTRY_COMPARE(target.presses(), 1);
    QCOMPARE(target.lastPress(), QPoint(1800, 1005));
    QCOMPARE(below.presses(), 0);
}
