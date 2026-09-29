/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QSaveFile>
#include <QScopeGuard>
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

    // KWin sees an explicit input shape at the drawable's size under Xwayland
    // 24.1.6, which is what the effect repairs, and scaled with the viewport to
    // the frame under the 24.1.10 of Ubuntu 26.04. What follows holds either way.
    const auto inputBounds = [this](const QSize &drawable) {
        const QString reported = status();
        const auto bounds = [](const QSize &size) {
            return QStringLiteral("inputBounds: 0,0,%1,%2").arg(size.width()).arg(size.height());
        };
        return reported.contains(bounds(drawable)) || reported.contains(bounds(drawable * 2));
    };
    target.inputShape(QRect(0, 0, 1920, 1080));
    QTRY_VERIFY(inputBounds(QSize(1920, 1080)));
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
    QTRY_VERIFY(inputBounds(QSize(960, 540)));
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

// A program that confines the pointer to its window, as Wine does for a
// fullscreen game, is given that by Xwayland as a confinement of its surface,
// which KWin checks in the surface's own coordinates and not in the picture the
// effect presents. While it lasts the pointer passes one to one, so that the
// program reaches every point of its window rather than two thirds of it.
void UpscaleX11IntegrationTest::aConfinedPointerReachesTheWholeWindow()
{
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    movePointer(logical(QPoint(100, 100)));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    QVERIFY(target.confinePointer());
    QTRY_VERIFY2_WITH_TIMEOUT(status().contains(QStringLiteral("pointerConfinement: engaged")), qPrintable(status()), 5000);
    // Near the window's own lower right, one to one: presented, it would have
    // arrived as half of that and the rest of the window been out of reach. A
    // multiple of the scale, 3, so that no rounding stands between the two.
    movePointer(logical(QPoint(1800, 1050)));
    QTRY_COMPARE(target.lastMotion(), QPoint(1800, 1050));
}

// A focus policy that follows the pointer activates the window KWin's own hit
// test finds, which beyond a presented window's own rectangle is the one
// underneath it. The keyboard has to stay with the game the user sees there.
// KWin's own protection of a fullscreen window is what keeps it; this holds the
// combination, because the effect cannot keep KWin's hit test off that window.
void UpscaleX11IntegrationTest::keepsTheKeyboardWhereThePointerIs()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup windows(config, QStringLiteral("Windows"));
    const QString previousPolicy = windows.readEntry("FocusPolicy", "ClickToFocus");
    const bool hadPolicy = windows.hasKey("FocusPolicy");
    windows.writeEntry("FocusPolicy", "FocusFollowsMouse");
    windows.sync();
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"),
                        QDBusConnection::sessionBus());
    const auto restorePolicy = qScopeGuard([&] {
        if (hadPolicy) {
            windows.writeEntry("FocusPolicy", previousPolicy);
        } else {
            windows.deleteEntry("FocusPolicy");
        }
        windows.sync();
        kwin.call(QStringLiteral("reconfigure"));
    });
    kwin.call(QStringLiteral("reconfigure"));

    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    // Whether the policy is in force at all: the pointer alone activates this
    // window while nothing is presented over it.
    movePointer(logical(QPoint(2880, 1620)));
    QTRY_VERIFY2(below.isFocused(), "the focus policy does not follow the pointer in this session");
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QTRY_VERIFY(target.isFocused());

    // Into the part of the output the window's own rectangle does not cover,
    // where KWin's hit test finds the window underneath.
    movePointer(logical(QPoint(2900, 1640)));
    QTRY_COMPARE(target.lastMotion(), QPoint(1450, 820));
    // Longer than KWin waits before focus follows the pointer, 300 ms by
    // default: what is proved is that nothing happens, and nothing signals it.
    QTest::qWait(500);
    QVERIFY2(target.isFocused(), "the window lost the keyboard to the window under it");
    QCOMPARE(target.focusLosses(), 0);
}

// Mouse look: the game hides the cursor and grabs the pointer, which Xwayland
// turns into a request to lock it. KWin takes that lock only while its own focus
// is on the game's own rectangle, so with the cursor anywhere else in the picture
// the lock would stay unanswered and the game's view would not turn.
void UpscaleX11IntegrationTest::letsAPresentedGameLockThePointer()
{
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));

    // The cursor where the window's own rectangle is not, and the game taking
    // the pointer from there.
    movePointer(logical(QPoint(2880, 1620)));
    QTRY_COMPARE(target.lastMotion(), QPoint(1440, 810));
    QVERIFY(target.takePointer());
    movePointer(logical(QPoint(2884, 1624)));
    QTRY_VERIFY2_WITH_TIMEOUT(status().contains(QStringLiteral("pointerLock: engaged")), qPrintable(status()), 5000);
}
