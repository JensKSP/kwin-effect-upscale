/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Where the pointer lands on a Wayland game whose picture has bars beside it:
// on the picture, as the game maps its surface onto its buffer, and never in a
// bar while the game holds it confined. And on a window drawn over its whole
// output, beside the window as well as over it.

#include "integration_test.h"
#include "wayland_client.h"

#include <QDBusReply>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSocketNotifier>
#include <QTest>

#include <cmath>

// Read and removed by the test driver inside the compositor, which moves its
// pointer device there; see the X11 test's movePointer().
static void movePointer(const QPoint &position)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-pointer");
    QSaveFile request(path);
    QVERIFY(request.open(QIODevice::WriteOnly));
    QVERIFY(request.write(QByteArray::number(position.x()) + ' ' + QByteArray::number(position.y())) > 0);
    QVERIFY(request.commit());
    QTRY_VERIFY(!QFile::exists(path));
}

// A left click there, which the test driver makes as it moves the pointer.
static void clickPointer(const QPoint &position)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-click");
    QSaveFile request(path);
    QVERIFY(request.open(QIODevice::WriteOnly));
    QVERIFY(request.write(QByteArray::number(position.x()) + ' ' + QByteArray::number(position.y())) > 0);
    QVERIFY(request.commit());
    QTRY_VERIFY(!QFile::exists(path));
}

// The same place within the 1/256 a Wayland coordinate is counted in.
static bool near(const QPointF &arrived, const QPointF &expected)
{
    return std::abs(arrived.x() - expected.x()) < 0.01 && std::abs(arrived.y() - expected.y()) < 0.01;
}

// 64 × 80 on the session's 128 × 128 screen is enlarged 1.6 times to
// 102 × 128, 13 pixels in from the left, and the game's surface still covers
// the screen. A point on the picture reaches the surface where the game drew
// what is under it: its offset into the picture, scaled up by 128 / 102.
void UpscaleIntegrationTest::mapsThePointerOntoThePicture()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    // Its own limit, because an earlier case in the session raises the global
    // one to this screen's size.
    writeCatalogue(integrationEntry(QStringLiteral("MinimumPixels=0\n")));
    configure(false, false);
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(QSize(64, 80)));
    QTRY_VERIFY2(status().contains(QStringLiteral("fitted into 102 × 128 with bars")), qPrintable(status()));
    const double across = 128.0 / 102.0;
    movePointer(QPoint(20, 64));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF((20 - 13) * across, 64)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    movePointer(QPoint(100, 30));
    QTRY_VERIFY(near(game.lastMotion(), QPointF((100 - 13) * across, 30)));

    // Confined, the pointer stops at the picture's edge rather than going on
    // into the bar, where it would reach nothing of the game.
    QVERIFY(game.confinePointer());
    QTRY_VERIFY2(game.pointerConfined(), qPrintable(status()));
    movePointer(QPoint(4, 64));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(0, 64)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    movePointer(QPoint(124, 64));
    QTRY_VERIFY(near(game.lastMotion(), QPointF((114 - 13) * across, 64)));
}

// A plain window its program sized to the smaller screen it was told, which is
// what GLFW 3.4 opens for a fullscreen video mode on Wayland, is drawn over its
// whole output. The 85 × 85 window lies somewhere on the 128 × 128
// screen, and its picture covers all of it, so a point on the screen reaches
// the surface at 85 / 128 of its position: over the window, beside it, where
// KWin's own hit test finds no window at all, and over it again. A click
// beside it reaches the game as well.
void UpscaleIntegrationTest::drawsAWindowOfTheToldSizeOverItsOutput()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        configureResolution(true, false, {});
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedMode\nMinimumPixels=0\nOrder=1\n")));
    configureResolution(true, false, Stored::Quality);
    WaylandClient game;
    QVERIFY(game.initialize(false));
    QCOMPARE(game.advertisedMode(), QSize(85, 85));
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    game.resize(QSize(85, 85));
    QVERIFY(game.show(QSize(85, 85)));
    QTRY_VERIFY2(status().contains(QStringLiteral("Supplied input: 85 × 85")) && status().contains(QStringLiteral("FSR 1, sharpening")),
                 qPrintable(status()));
    const double across = 85.0 / 128.0;
    movePointer(QPoint(64, 64));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(64 * across, 64 * across)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    movePointer(QPoint(124, 124));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(124 * across, 124 * across)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    clickPointer(QPoint(124, 124));
    QTRY_COMPARE(game.presses(), 1);
    // Back onto the window, where KWin's own hit test enters its surface again
    // and would leave the position at its own mapping.
    movePointer(QPoint(60, 60));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(60 * across, 60 * across)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
}
