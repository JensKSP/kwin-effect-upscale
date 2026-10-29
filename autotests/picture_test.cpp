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
    void asksForAWholeFactorsShare();
    void reachesAWholeFactorsShareInScaleSteps();
    void drawsAWholeFactorOfOneAsACopy();
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
    // The classic filters enlarge by any amount, as nearest does.
    QTest::newRow("Lanczos further") << QSize(640, 480) << fit << UpscaleFilter::Lanczos << yes << QRect(480, 0, 2880, 2160) << 0;
    QTest::newRow("integer three with bicubic") << QSize(1280, 720) << whole << UpscaleFilter::Bicubic << yes << QRect(0, 0, 3840, 2160) << 3;
    QTest::newRow("integer one with bilinear") << QSize(2560, 1440) << whole << UpscaleFilter::Bilinear << yes << QRect(640, 360, 2560, 1440) << 1;
    QTest::newRow("GSR further") << QSize(1280, 720) << fit << UpscaleFilter::Gsr << yes << QRect(0, 0, 3840, 2160) << 0;
    // NIS keeps NVIDIA's range, which is FSR's.
    QTest::newRow("NIS at most twice") << QSize(1280, 720) << fit << UpscaleFilter::Nis << UpscaleSizing::BelowHalf << QRect() << 0;
    QTest::newRow("NIS twice") << QSize(1920, 1080) << fit << UpscaleFilter::Nis << yes << QRect(0, 0, 3840, 2160) << 0;
    QTest::newRow("integer three with NIS") << QSize(1280, 720) << whole << UpscaleFilter::Nis << UpscaleSizing::FilterRange << QRect() << 3;
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

// Integer is told the share of the whole factor nearest the resolution's, so
// that what it asks for is what it enlarges (Jens, 2026-10-13): Quality's two
// thirds is told half, which FSR enlarges by two, rather than 2560 × 1440,
// which it refused under Integer.
void PictureTest::asksForAWholeFactorsShare()
{
    const UpscaleSize output{3840, 2160};
    const auto whole = UpscaleGeometry::Integer;
    QCOMPARE(upscaleWholeFactor(resolutionRatio(ResolutionPreset::UltraQuality, 0)), 1);
    QCOMPARE(upscaleWholeFactor(resolutionRatio(ResolutionPreset::Quality, 0)), 2);
    QCOMPARE(upscaleWholeFactor(resolutionRatio(ResolutionPreset::Balanced, 0)), 2);
    QCOMPARE(upscaleWholeFactor(resolutionRatio(ResolutionPreset::Performance, 0)), 2);
    QCOMPARE(upscaleWholeFactor(resolutionRatio(ResolutionPreset::Native, 0)), 1);
    // Three quarters lies as near the whole output as half of it: the smaller
    // wins, since a resolution below native asked for less. Just above, all.
    QCOMPARE(upscaleWholeFactor(0.75), 2);
    QCOMPARE(upscaleWholeFactor(0.7501), 1);
    QCOMPARE(upscaleWholeFactor(0.3), 3);
    QCOMPARE(upscaleWholeFactor(0.0), 1);
    QCOMPARE(upscaleRequestSize(output, resolutionRatio(ResolutionPreset::Quality, 0), whole), (UpscaleSize{1920, 1080}));
    QCOMPARE(upscaleRequestSize(output, resolutionRatio(ResolutionPreset::UltraQuality, 0), whole), output);
    QCOMPARE(upscaleRequestSize(output, resolutionRatio(ResolutionPreset::Quality, 0), UpscaleGeometry::Fit),
             (UpscaleSize{2560, 1440}));
    // Half an odd output is divided down, so that two times it still fits.
    const UpscaleSize odd = upscaleRequestSize({1365, 767}, 0.5, whole);
    QCOMPARE(odd, (UpscaleSize{682, 383}));
    QCOMPARE(upscalePicture(odd, {1365, 767}, whole, UpscaleFilter::Fsr).sizing, UpscaleSizing::Supported);
    // An X11 output's list offers Integer the wish itself or nothing.
    const std::vector<UpscaleSize> listed{{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}};
    QCOMPARE(upscaleNearestListedSize(listed, {1920, 1080}, output, whole), (UpscaleSize{1920, 1080}));
    QCOMPARE(upscaleNearestListedSize({{3840, 2160}, {2560, 1440}}, {1920, 1080}, output, whole), (UpscaleSize{0, 0}));
    QCOMPARE(upscaleNearestListedSize({{3840, 2160}, {2560, 1440}}, {1920, 1080}, output, UpscaleGeometry::Fit),
             (UpscaleSize{2560, 1440}));
}

// A client moved in whole steps of the output's scale is told the step Integer
// enlarges by the factor asked for, or nothing where no step gives it.
void PictureTest::reachesAWholeFactorsShareInScaleSteps()
{
    const UpscaleSize output{3840, 2160};
    const auto whole = UpscaleGeometry::Integer;
    // At scale two, one is half the output, twice enlarged.
    QCOMPARE(upscaleReachableScale(output, 2, ResolutionPreset::Quality, 0, whole), 1);
    // At scale three, a third is enlarged three times and two thirds once:
    // neither is two. Fit takes two thirds, Quality's own share.
    QCOMPARE(upscaleReachableScale(output, 3, ResolutionPreset::Quality, 0, whole), 0);
    QCOMPARE(upscaleReachableScale(output, 3, ResolutionPreset::Quality, 0, UpscaleGeometry::Fit), 2);
    // Two fifths is enlarged twice, between bars.
    QCOMPARE(upscaleReachableScale(output, 2.5, ResolutionPreset::Performance, 0, whole), 1);
    // A factor of one asks nothing.
    QCOMPARE(upscaleReachableScale(output, 2, ResolutionPreset::UltraQuality, 0, whole), 0);
}

// A whole factor of one enlarges nothing, so whatever was chosen draws it as
// the copy it is, and the status names that rather than the filter.
void PictureTest::drawsAWholeFactorOfOneAsACopy()
{
    const UpscaleSize output{3840, 2160};
    const UpscalePicture one = upscalePicture({2560, 1440}, output, UpscaleGeometry::Integer, UpscaleFilter::Lanczos);
    QCOMPARE(one.factor, 1);
    QCOMPARE(upscaleDrawnFilter(UpscaleFilter::Lanczos, one), UpscaleFilter::Nearest);
    const UpscalePicture two = upscalePicture({1920, 1080}, output, UpscaleGeometry::Integer, UpscaleFilter::Lanczos);
    QCOMPARE(upscaleDrawnFilter(UpscaleFilter::Lanczos, two), UpscaleFilter::Lanczos);
    const UpscalePicture fitted = upscalePicture({2560, 1440}, output, UpscaleGeometry::Fit, UpscaleFilter::Lanczos);
    QCOMPARE(upscaleDrawnFilter(UpscaleFilter::Lanczos, fitted), UpscaleFilter::Lanczos);
}

QTEST_GUILESS_MAIN(PictureTest)

#include "picture_test.moc"
