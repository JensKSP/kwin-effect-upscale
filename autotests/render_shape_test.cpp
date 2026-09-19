/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the scaler has to survive about the shape of a frame: the orientation
// KWin composes into, and sizes that are not the tidy powers of two a fixture
// would otherwise use.

#include "render_fixture.h"
#include "resolution.h"

#include <QTest>

#include <array>
#include <cmath>
#include <utility>
#include <vector>

using namespace KWin;

class UpscaleRenderShapeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void drawsIntoATransformedTarget();
    void scalesOddSizes();

private:
    UpscaleRenderFixture m_fixture;
};

void UpscaleRenderShapeTest::initTestCase()
{
    QVERIFY(m_fixture.initialize());
}

void UpscaleRenderShapeTest::cleanupTestCase()
{
    m_fixture.release();
}

// KWin composes the screen into a target whose content is flipped: its DRM
// backend begins every frame with the output's transform combined with
// OutputTransform::FlipY, so on an upright screen every frame arrives here
// flipped. The projection matrix a RenderViewport hands out already carries
// that transform, so the scaler has nothing to do about it, and this test is
// what says so: the same drawing into a flipped target has to land mirrored
// in memory and be identical otherwise.
void UpscaleRenderShapeTest::drawsIntoATransformedTarget()
{
    const QSize inputSize(8, 8);
    const QSize outputSize(16, 16);
    std::vector<float> input(8 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const size_t pixel = (size_t(y) * 8 + size_t(x)) * 4;
            input[pixel] = float(x) / 7;
            input[pixel + 1] = float(y) / 7;
            input[pixel + 2] = 0.25;
            input[pixel + 3] = 1;
        }
    }
    const TransferFunction transfer(TransferFunction::gamma22);
    for (const double strength : {0.0, 0.5}) {
        const std::vector<float> upright = m_fixture.render(input, inputSize, outputSize, transfer, strength);
        QCOMPARE(upright.size(), size_t(16 * 16 * 4));
        const std::vector<float> flipped = m_fixture.render(input, inputSize, outputSize, transfer, strength,
                                                            unlimitedRegion(), OutputTransform::FlipY);
        QCOMPARE(flipped.size(), upright.size());
        const size_t stride = size_t(outputSize.width()) * 4;
        for (int row = 0; row < outputSize.height(); ++row) {
            const size_t mirrored = size_t(outputSize.height() - 1 - row) * stride;
            for (size_t index = 0; index < stride; ++index) {
                QVERIFY2(std::abs(flipped[size_t(row) * stride + index] - upright[mirrored + index]) <= 0.0005,
                         qPrintable(QStringLiteral("row %1 index %2 strength %3").arg(row).arg(index).arg(strength)));
            }
        }
    }
}

// Nothing guarantees that a game's buffer or an output has even dimensions,
// and a calculated ratio rarely produces them: 1 / 1.7 of 3840 x 2160 is
// 2259 x 1271. These ratios are the awkward ones the presets actually make,
// with odd widths, odd heights and a destination that is not a whole multiple
// of the source, so a rounding mistake in either shader pass shows up as a
// wrong corner rather than staying hidden behind power-of-two fixtures.
void UpscaleRenderShapeTest::scalesOddSizes()
{
    const std::array<std::pair<QSize, QSize>, 4> sizes = {
        std::pair{QSize(9, 5), QSize(16, 9)},
        std::pair{QSize(15, 9), QSize(27, 16)},
        std::pair{QSize(13, 7), QSize(25, 13)},
        std::pair{QSize(7, 7), QSize(13, 13)},
    };
#if UPSCALE_REGION_API
    const double reference = ColorDescription::sRGB->referenceLuminance();
#else
    const double reference = ColorDescription::sRGB.referenceLuminance();
#endif
    const TransferFunction transfer(TransferFunction::linear, 0, reference);
    for (const auto &[inputSize, outputSize] : sizes) {
        // The effect must never be asked to scale a pair it would refuse.
        QVERIFY(canUpscale({inputSize.width(), inputSize.height()}, {outputSize.width(), outputSize.height()}));
        std::vector<float> input(size_t(inputSize.width()) * size_t(inputSize.height()) * 4, 0);
        for (int y = 0; y < inputSize.height(); ++y) {
            for (int x = 0; x < inputSize.width(); ++x) {
                const size_t pixel = (size_t(y) * size_t(inputSize.width()) + size_t(x)) * 4;
                input[pixel] = x < inputSize.width() / 2 ? 0.0F : 1.0F;
                input[pixel + 1] = y < inputSize.height() / 2 ? 0.0F : 1.0F;
                input[pixel + 2] = 0.18F;
                input[pixel + 3] = 1;
            }
        }
        for (const double strength : {0.0, 1.0}) {
            const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, transfer, strength);
            QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
            for (const float value : result) {
                QVERIFY(std::isfinite(value));
            }
            for (const int y : {0, outputSize.height() - 1}) {
                for (const int x : {0, outputSize.width() - 1}) {
                    const size_t pixel = (size_t(y) * size_t(outputSize.width()) + size_t(x)) * 4;
                    const QString where = QStringLiteral("%1x%2 -> %3x%4 at %5,%6 strength %7")
                                              .arg(inputSize.width())
                                              .arg(inputSize.height())
                                              .arg(outputSize.width())
                                              .arg(outputSize.height())
                                              .arg(x)
                                              .arg(y)
                                              .arg(strength);
                    QVERIFY2(std::abs(result[pixel] - (x == 0 ? 0.0F : 1.0F)) < 0.001F, qPrintable(where));
                    QVERIFY2(std::abs(result[pixel + 1] - (y == 0 ? 0.0F : 1.0F)) < 0.001F, qPrintable(where));
                    QVERIFY2(std::abs(result[pixel + 2] - 0.18F) < 0.001F, qPrintable(where));
                }
            }
        }
    }
}

QTEST_GUILESS_MAIN(UpscaleRenderShapeTest)

#include "render_shape_test.moc"
