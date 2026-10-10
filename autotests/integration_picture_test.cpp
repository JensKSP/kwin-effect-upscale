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

// A motion by @p delta with no position of its own, as a mouse reports one,
// which the test driver makes the same way.
static void movePointerBy(const QPoint &delta)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-pointer");
    QSaveFile request(path);
    QVERIFY(request.open(QIODevice::WriteOnly));
    QVERIFY(request.write("by " + QByteArray::number(delta.x()) + ' ' + QByteArray::number(delta.y())) > 0);
    QVERIFY(request.commit());
    QTRY_VERIFY(!QFile::exists(path));
}

// "down <id> <x> <y>", "move <id> <x> <y>" or "up <id>" on the test driver's
// touch screen.
static void touchScreen(const QByteArray &request)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-touch");
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(request) > 0);
    QVERIFY(file.commit());
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

// Relative motion, which a game's mouse look reads, counts in the surface's own
// coordinates as well, on a picture with bars beside it or above and below it,
// and while the game holds the pointer locked only relative motion arrives,
// scaled the same. Fit enlarges 64 × 80 to 102 × 128, 13 pixels in from the
// left; Integer enlarges 64 × 48 twice, to 128 × 96, 16 pixels down.
void UpscaleIntegrationTest::carriesRelativeMotionOntoThePicture_data()
{
    QTest::addColumn<QString>("geometry");
    QTest::addColumn<QSize>("buffer");
    QTest::addColumn<QString>("shown");
    QTest::addColumn<QPointF>("origin");
    QTest::addColumn<QPointF>("across");
    QTest::newRow("fit") << QStringLiteral("Fit") << QSize(64, 80) << QStringLiteral("fitted into 102 × 128 with bars") << QPointF(13, 0)
                         << QPointF(128.0 / 102.0, 1);
    QTest::newRow("integer") << QStringLiteral("Integer") << QSize(64, 48) << QStringLiteral("enlarged 2 times") << QPointF(0, 16)
                             << QPointF(1, 128.0 / 96.0);
}

void UpscaleIntegrationTest::carriesRelativeMotionOntoThePicture()
{
    QFETCH(QString, geometry);
    QFETCH(QSize, buffer);
    QFETCH(QString, shown);
    QFETCH(QPointF, origin);
    QFETCH(QPointF, across);
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    writeCatalogue(integrationEntry(QStringLiteral("MinimumPixels=0\nGeometry=%1\n").arg(geometry)));
    configure(false, false);
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(buffer));
    QTRY_VERIFY2(status().contains(shown), qPrintable(status()));
    QVERIFY(game.watchRelativeMotion());
    const auto surface = [&](const QPointF &point) {
        return QPointF((point.x() - origin.x()) * across.x(), (point.y() - origin.y()) * across.y());
    };
    const auto said = [&game]() {
        return QStringLiteral("%1, %2").arg(game.relativeMotion().x()).arg(game.relativeMotion().y());
    };
    movePointer(QPoint(40, 64));
    QTRY_VERIFY(near(game.lastMotion(), surface(QPointF(40, 64))));
    game.resetRelativeMotion();
    movePointerBy(QPoint(20, 12));
    QTRY_VERIFY2(near(game.relativeMotion(), QPointF(20 * across.x(), 12 * across.y())), qPrintable(said()));
    QTRY_VERIFY(near(game.lastMotion(), surface(QPointF(60, 76))));

    QVERIFY(game.lockPointer());
    QTRY_VERIFY2(game.pointerLocked(), qPrintable(status()));
    const QPointF held = game.lastMotion();
    game.resetRelativeMotion();
    movePointerBy(QPoint(-30, 6));
    QTRY_VERIFY2(near(game.relativeMotion(), QPointF(-30 * across.x(), 6 * across.y())), qPrintable(said()));
    QCOMPARE(game.lastMotion(), held);
}

// A popup of the game's lies where the game's surface coordinates put it,
// which KWin draws unenlarged, and over the bar left of the picture here. The
// pointer there is the popup's, at the popup's own coordinates, and the game
// hears nothing of it.
void UpscaleIntegrationTest::leavesAPopupOverTheBarsItsOwnPointer()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
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
    movePointer(QPoint(40, 64));
    QTRY_VERIFY(near(game.lastMotion(), QPointF((40 - 13) * 128.0 / 102.0, 64)));
    const QPointF before = game.lastMotion();
    QVERIFY(game.openPopup(QRect(2, 40, 8, 8)));
    movePointer(QPoint(5, 43));
    QTRY_VERIFY2(near(game.popupMotion(), QPointF(3, 3)),
                 qPrintable(QStringLiteral("%1, %2\n%3").arg(game.popupMotion().x()).arg(game.popupMotion().y()).arg(status())));
    QCOMPARE(game.lastMotion(), before);
}

// Touch lands where the picture shows it, as the pointer does: on the picture
// with bars beside it, a touch at 40, 64 reaches the game at (40 - 13) × 128 /
// 102 across, its motion follows the picture, and a touch in a bar reaches
// nothing of the game.
void UpscaleIntegrationTest::mapsTouchOntoThePicture()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
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
    QTRY_VERIFY(game.hasTouch());
    const double across = 128.0 / 102.0;
    touchScreen("down 0 40 64");
    QTRY_VERIFY2(near(game.lastTouch(), QPointF((40 - 13) * across, 64)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastTouch().x()).arg(game.lastTouch().y())));
    touchScreen("move 0 60 70");
    QTRY_VERIFY2(near(game.lastTouch(), QPointF((60 - 13) * across, 70)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastTouch().x()).arg(game.lastTouch().y())));
    touchScreen("up 0");
    touchScreen("down 1 5 64");
    touchScreen("up 1");
    QVERIFY(game.roundtrip());
    QCOMPARE(game.touchesDown(), 1);
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
    // A touch beside the window as well, where KWin finds no window to touch.
    QTRY_VERIFY(game.hasTouch());
    touchScreen("down 0 124 124");
    QTRY_VERIFY2(near(game.lastTouch(), QPointF(124 * across, 124 * across)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastTouch().x()).arg(game.lastTouch().y())));
    touchScreen("up 0");
}

// A client that ignores fractional scale, as Qt does below one, but sizes its
// buffer to the fullscreen configure. Auto must make that configure smaller,
// carry input back from the enlarged picture, and restore the original size.
void UpscaleIntegrationTest::autoResizesAClientThatIgnoresScale()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        configureResolution(true, false, {});
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=Auto\nMinimumPixels=0\nOrder=1\n")));
    configureResolution(true, false, Stored::Quality);
    configureDisplay(false, false);
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(QSize(128, 128)));
    QVERIFY(game.presentFrames(35));
    QTRY_COMPARE(game.configuredSize(), QSize(85, 85));
    QVERIFY(game.show(game.configuredSize()));
    QTRY_VERIFY2(status().contains(QStringLiteral("Supplied input: 85 × 85")) && status().contains(QStringLiteral("FSR 1, sharpening")),
                 qPrintable(status()));
    QCOMPARE(game.preferredScale(), 120);
    const double across = 85.0 / 128.0;
    movePointer(QPoint(64, 64));
    QTRY_VERIFY(near(game.lastMotion(), QPointF(64 * across, 64 * across)));
    movePointer(QPoint(124, 124));
    QTRY_VERIFY(near(game.lastMotion(), QPointF(124 * across, 124 * across)));
    clickPointer(QPoint(124, 124));
    QTRY_COMPARE(game.presses(), 1);

    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=Off\nMinimumPixels=0\nOrder=1\n")));
    configureResolution(false, false, Stored::Quality);
    QTRY_COMPARE(game.configuredSize(), QSize(128, 128));
    QVERIFY(game.show(game.configuredSize()));
    QVERIFY(game.presentFrames(2));
    movePointer(QPoint(60, 60));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(60, 60)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
}

void UpscaleIntegrationTest::autoConfiguresAnIntegerClientBeforeItsFirstBuffer_data()
{
    QTest::addColumn<QString>("method");
    QTest::addColumn<QSize>("configured");
    QTest::newRow("auto") << QStringLiteral("Auto") << QSize(85, 85);
    QTest::newRow("off") << QStringLiteral("Off") << QSize(128, 128);
    QTest::newRow("explicit-scale") << QStringLiteral("AdvertisedScale") << QSize(128, 128);
}

void UpscaleIntegrationTest::autoConfiguresAnIntegerClientBeforeItsFirstBuffer()
{
    QFETCH(QString, method);
    QFETCH(QSize, configured);
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        configureResolution(true, false, {});
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=%1\nMinimumPixels=0\nOrder=1\n").arg(method)));
    configureResolution(true, false, Stored::Quality);
    configureDisplay(false, false);
    WaylandClient game(2, false);
    QVERIFY(game.initialize());
    // Before show() allocates the first buffer: a late resize cannot change a
    // viewport that an application initializes only once, as glmark2 does.
    QCOMPARE(game.configuredSize(), configured);
    QCOMPARE(game.preferredScale(), 0);
    // The early hook must respect an explicit method as well as Auto: Off
    // leaves the original size, and an advertisement never changes geometry.
    if (method != QLatin1String("Auto")) {
        return;
    }
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(game.configuredSize()));
    QTRY_VERIFY2(status().contains(QStringLiteral("Supplied input: 85 × 85")) && status().contains(QStringLiteral("FSR 1, sharpening")),
                 qPrintable(status()));
    movePointer(QPoint(124, 124));
    QTRY_VERIFY(near(game.lastMotion(), QPointF(124 * 85.0 / 128.0, 124 * 85.0 / 128.0)));
    clickPointer(QPoint(124, 124));
    QTRY_COMPARE(game.presses(), 1);
}
