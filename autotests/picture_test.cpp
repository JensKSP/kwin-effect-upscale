/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Where the enlarged picture goes on its output, for each geometry and filter,
// with the handbook's own examples among the cases.

#include "picture.h"
#include "resolution.h"

#include <QTest>

using namespace KWin;

Q_DECLARE_METATYPE(KWin::UpscaleGeometry)
Q_DECLARE_METATYPE(KWin::UpscaleFilter)
Q_DECLARE_METATYPE(KWin::UpscaleSizing)

class PictureTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void placesThePicture_data();
    void placesThePicture();
    void choosesTheNearestListedSize();
};

void PictureTest::placesThePicture_data()
{
    QTest::addColumn<QSize>("input");
    QTest::addColumn<UpscaleGeometry>("geometry");
    QTest::addColumn<UpscaleFilter>("filter");
    QTest::addColumn<UpscaleSizing>("sizing");
    QTest::addColumn<QRect>("placed");
    QTest::addColumn<int>("factor");
    const auto fit = UpscaleGeometry::Fit;
    const auto whole = UpscaleGeometry::Integer;
    const auto fsr = UpscaleFilter::Fsr;
    const auto nearest = UpscaleFilter::Nearest;
    const auto yes = UpscaleSizing::Supported;
    QTest::newRow("same aspect fills") << QSize(2560, 1440) << fit << fsr << yes << QRect(0, 0, 3840, 2160) << 0;
    QTest::newRow("a client's rounding fills") << QSize(2559, 1440) << fit << fsr << yes << QRect(0, 0, 3840, 2160) << 0;
    // The handbook's Fit example: 480-pixel bars on each side.
    QTest::newRow("four by three") << QSize(1440, 1080) << fit << fsr << yes << QRect(480, 0, 2880, 2160) << 0;
    QTest::newRow("FSR at most twice") << QSize(1280, 720) << fit << fsr << UpscaleSizing::BelowHalf << QRect() << 0;
    QTest::newRow("nearest further") << QSize(640, 480) << fit << nearest << yes << QRect(480, 0, 2880, 2160) << 0;
    QTest::newRow("wider than the output") << QSize(3000, 1000) << fit << nearest << yes << QRect(0, 440, 3840, 1280) << 0;
    // The handbook's Integer example: 9 times, bars left and right.
    QTest::newRow("integer nine") << QSize(320, 240) << whole << nearest << yes << QRect(480, 0, 2880, 2160) << 9;
    QTest::newRow("integer one centres") << QSize(2560, 1440) << whole << nearest << yes << QRect(640, 360, 2560, 1440) << 1;
    QTest::newRow("integer two with FSR") << QSize(1920, 1080) << whole << fsr << yes << QRect(0, 0, 3840, 2160) << 2;
    QTest::newRow("integer one with FSR") << QSize(2560, 1440) << whole << fsr << UpscaleSizing::FilterRange << QRect() << 1;
    QTest::newRow("integer three with FSR") << QSize(1280, 720) << whole << fsr << UpscaleSizing::FilterRange << QRect() << 3;
    // Bars that cannot be equal differ by one pixel, the extra one right.
    QTest::newRow("odd bars") << QSize(1999, 1000) << whole << nearest << yes << QRect(920, 580, 1999, 1000) << 1;
    QTest::newRow("no whole factor") << QSize(4000, 2000) << whole << nearest << UpscaleSizing::NoWholeFactor << QRect() << 0;
    QTest::newRow("native, integer") << QSize(3840, 2160) << whole << nearest << UpscaleSizing::NotSmaller << QRect() << 0;
    QTest::newRow("native, fit") << QSize(3840, 2160) << fit << nearest << UpscaleSizing::NotSmaller << QRect() << 0;
    QTest::newRow("larger, fit") << QSize(4096, 2160) << fit << nearest << UpscaleSizing::NotSmaller << QRect() << 0;
    QTest::newRow("empty") << QSize(0, 0) << fit << fsr << UpscaleSizing::EmptyBuffer << QRect() << 0;
}

void PictureTest::placesThePicture()
{
    QFETCH(QSize, input);
    QFETCH(UpscaleGeometry, geometry);
    QFETCH(UpscaleFilter, filter);
    QFETCH(UpscaleSizing, sizing);
    QFETCH(QRect, placed);
    QFETCH(int, factor);
    const UpscalePicture picture = upscalePicture({input.width(), input.height()}, {3840, 2160}, geometry, filter);
    QCOMPARE(picture.sizing, sizing);
    QCOMPARE(picture.factor, factor);
    if (sizing == UpscaleSizing::Supported) {
        QCOMPARE(QRect(picture.x, picture.y, picture.width, picture.height), placed);
    }
}

// An X11 game is given one of the modes its output lists, Xwayland's own for
// a 3840 × 2160 output among them. Balanced wishes for 2259 × 1271, which none
// is: the nearest the scaler enlarges is asked instead, 2048 × 1152.
void PictureTest::choosesTheNearestListedSize()
{
    const UpscaleSize output{3840, 2160};
    const std::vector<UpscaleSize> listed{{3840, 2160}, {3200, 1800}, {2880, 1620}, {2560, 1600}, {2560, 1440}, {2048, 1536}, {2048, 1152}, {1920, 1440}, {1920, 1200}, {1920, 1080}, {1600, 900}, {1280, 720}};
    QCOMPARE(nearestListedSize(listed, {1920, 1080}, output), (UpscaleSize{1920, 1080}));
    QCOMPARE(nearestListedSize(listed, {2259, 1271}, output), (UpscaleSize{2048, 1152}));
    QCOMPARE(nearestListedSize(listed, {2400, 1350}, output), (UpscaleSize{2560, 1440}));
    QCOMPARE(nearestListedSize(listed, {1960, 1103}, output), (UpscaleSize{1920, 1080}));
    // Of two equally near, the larger, which leaves the scaler less to make up.
    QCOMPARE(nearestListedSize({{1920, 1080}, {2560, 1440}}, {2240, 1260}, output), (UpscaleSize{2560, 1440}));
    // Neither the output's own size, another shape, nor one below half is offered.
    QCOMPARE(nearestListedSize({{3840, 2160}, {2048, 1536}, {1600, 900}}, {2259, 1271}, output), (UpscaleSize{0, 0}));
}

QTEST_GUILESS_MAIN(PictureTest)

#include "picture_test.moc"
