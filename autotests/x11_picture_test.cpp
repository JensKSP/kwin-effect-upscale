/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Where the pointer lands on an X11 game whose picture has bars beside it: a
// game in a mode of its own, which Xwayland's emulation maps the whole frame
// onto, and a drawable the effect asked to be smaller and presents itself.

#include "x11_client.h"
#include "x11_integration_test.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QTest>
#include <QVersionNumber>

// A 4:3 mode the game set itself on the 16:9 screen, as an older game does,
// fitted: 1440 × 1080 enlarged twice to 2880 × 2160, with 480 pixels of bar on
// either side. A point on the picture reaches the drawable where the game drew
// what is under it. Asked for nothing, so that the mode stays the game's.
void UpscaleX11IntegrationTest::fitsAnEmulatedModeBetweenBars()
{
    if (QVersionNumber::fromString(QStringLiteral(UPSCALE_TEST_KWIN_VERSION)) < QVersionNumber(6, 6)) {
        QSKIP("KWin 6.3 sizes a fullscreen X window to its output, not to the mode its game set");
    }
    KConfigGroup entry(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-test"));
    entry.writeEntry("MethodX11FullScreen", QStringLiteral("Off"));
    entry.sync();
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1440, 1080), false));
    QVERIFY(target.mode(QSize(1440, 1080)));
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_COMPARE(target.geometry(), QRect(0, 0, 1440, 1080));
    QTRY_VERIFY2(status().contains(QStringLiteral("fitted into 2880 × 2160 with bars")), qPrintable(status()));
    target.inputShape(QRect(0, 0, 1440, 1080));
    // Xwayland does not scale its first enter; see keepsEmulatedPointerCoverage.
    movePointer(QPoint(700, 300));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    // Across, a multiple of three, so that the frame coordinate between the
    // picture and the drawable, four thirds of it, is whole: Wayland carries
    // it in 256ths and Xwayland truncates, which would take a pixel off.
    movePointer(QPoint(480 + 198, 200));
    QTRY_COMPARE(target.lastMotion(), QPoint(99, 100));
    movePointer(QPoint(480 + 2004, 2000));
    QTRY_COMPARE(target.lastMotion(), QPoint(1002, 1000));
}

// A whole factor of one, stated by the game's entry: the 2560 × 1440 the
// effect asked for is shown as it is, centred, 640 and 360 pixels in.
void UpscaleX11IntegrationTest::centresAPresentedWindowByAWholeFactor()
{
    KConfigGroup entry(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-test"));
    entry.writeEntry("Geometry", QStringLiteral("Integer"));
    entry.writeEntry("Filter", QStringLiteral("Nearest"));
    entry.sync();
    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    configure(true, Stored::Quality);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QTRY_VERIFY2(status().contains(QStringLiteral("Nearest neighbor, enlarged 1 time")), qPrintable(status()));
    movePointer(logical(QPoint(700, 400)));
    QTRY_VERIFY(target.lastMotion() != QPoint(-1, -1));
    movePointer(logical(QPoint(640 + 300, 360 + 300)));
    QTRY_COMPARE(target.lastMotion(), QPoint(300, 300));
    movePointer(logical(QPoint(640 + 2400, 360 + 1200)));
    QTRY_COMPARE(target.lastMotion(), QPoint(2400, 1200));
}
