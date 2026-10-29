/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the filters taken from others draw: Snapdragon GSR 1, which sharpens
// as part of its filter. Its reference shader cannot run here as written, so
// it is held to what it computes exactly where it finds no edge, and to its
// documented bounds where it does.

#include "picture.h"
#include "render_fixture.h"

#include <QFile>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace KWin;

namespace
{

std::vector<float> picture(const QSize &size, auto value)
{
    std::vector<float> pixels(size_t(size.width()) * size_t(size.height()) * 4, 1);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                pixels[(size_t(y) * size_t(size.width()) + size_t(x)) * 4 + size_t(channel)] = float(value(x, y, channel));
            }
        }
    }
    return pixels;
}

float at(const std::vector<float> &pixels, const QSize &size, int x, int y, int channel)
{
    x = std::clamp(x, 0, size.width() - 1);
    y = std::clamp(y, 0, size.height() - 1);
    return pixels[(size_t(y) * size_t(size.width()) + size_t(x)) * 4 + size_t(channel)];
}

// The bilinear interpolation of one channel at output pixel (x, y), as both
// filters compute their base tap.
double bilinearAt(const std::vector<float> &pixels, const QSize &input, const QSize &output, int x, int y, int channel)
{
    const double u = (x + 0.5) * input.width() / output.width() - 0.5;
    const double v = (y + 0.5) * input.height() / output.height() - 0.5;
    const int i = int(std::floor(u));
    const int j = int(std::floor(v));
    const double fu = u - i;
    const double fv = v - j;
    const double top = at(pixels, input, i, j, channel) * (1 - fu) + at(pixels, input, i + 1, j, channel) * fu;
    const double bottom = at(pixels, input, i, j + 1, channel) * (1 - fu) + at(pixels, input, i + 1, j + 1, channel) * fu;
    return top * (1 - fv) + bottom * fv;
}

std::vector<float> render(UpscaleRenderFixture &fixture, const std::vector<float> &input, const QSize &inputSize, const QSize &outputSize,
                          UpscaleFilter filter, double strength, TransferFunction transfer = TransferFunction(TransferFunction::gamma22))
{
    return fixture.render(input, inputSize, outputSize, transfer, strength, unlimitedRegion(), OutputTransform::Normal, {}, filter);
}

} // namespace

class UpscaleRenderMethodTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void gsrLeavesAGentleGradientToItsBase();
    void gsrBoundsItsCorrection();
    void carriesQualcommsNotice();

private:
    UpscaleRenderFixture m_fixture;
};

void UpscaleRenderMethodTest::initTestCase()
{
    QVERIFY(m_fixture.initialize());
}

void UpscaleRenderMethodTest::cleanupTestCase()
{
    m_fixture.release();
}

// Qualcomm's filter changes a pixel only where the green channel's edge vote
// passes 8/255. Below it, at any strength, what remains is its bilinear tap.
void UpscaleRenderMethodTest::gsrLeavesAGentleGradientToItsBase()
{
    const QSize inputSize(9, 7);
    const QSize outputSize(16, 12);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int channel) {
        return 0.3 + 0.004 * x + 0.003 * y + 0.1 * channel;
    });
    for (const double strength : {0.0, 1.0}) {
        const std::vector<float> result = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Gsr, strength);
        QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
        for (int y = 0; y < outputSize.height(); ++y) {
            for (int x = 0; x < outputSize.width(); ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const double expected = bilinearAt(input, inputSize, outputSize, x, y, channel);
                    QVERIFY2(std::abs(at(result, outputSize, x, y, channel) - expected) < 1e-5,
                             qPrintable(QStringLiteral("%1,%2,%3: %4 for %5").arg(x).arg(y).arg(channel).arg(at(result, outputSize, x, y, channel)).arg(expected)));
                }
            }
        }
    }
}

// Past the edge vote it moves every channel by the same correction, never by
// more than the 23/255 Qualcomm allows, and its edge sharpness moves them
// further. The picture is a ramp steep enough to pass the vote: at a step the
// result already reaches the range of the four texels around the sample at
// either strength, which it is clamped to, and there is nothing to compare.
void UpscaleRenderMethodTest::gsrBoundsItsCorrection()
{
    const QSize inputSize(8, 6);
    const QSize outputSize(14, 10);
    const std::vector<float> input = picture(inputSize, [](int x, int, int channel) {
        return 0.2 + 0.06 * x + 0.05 * (channel - 1);
    });
    std::array<double, 2> moved{};
    for (const int index : {0, 1}) {
        const std::vector<float> result = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Gsr, index);
        QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
        for (int y = 0; y < outputSize.height(); ++y) {
            for (int x = 0; x < outputSize.width(); ++x) {
                const double correction = at(result, outputSize, x, y, 1) - bilinearAt(input, inputSize, outputSize, x, y, 1);
                QVERIFY2(std::abs(correction) <= 23.0 / 255.0 + 1e-5, qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(correction)));
                for (const int channel : {0, 2}) {
                    const double other = at(result, outputSize, x, y, channel) - bilinearAt(input, inputSize, outputSize, x, y, channel);
                    QVERIFY(std::abs(other - correction) < 1e-5);
                }
                moved[size_t(index)] += std::abs(correction);
            }
        }
    }
    QVERIFY2(moved[0] > 1e-3 && moved[1] > moved[0], qPrintable(QStringLiteral("%1 %2").arg(moved[0]).arg(moved[1])));
}

// Qualcomm's licence asks for its notice to go with every copy, and the
// shader reaches the plugin as a resource, so the resource keeps it.
void UpscaleRenderMethodTest::carriesQualcommsNotice()
{
    QFile shader(QStringLiteral(":/effects/upscale/shaders/gsr.frag"));
    QVERIFY(shader.open(QIODevice::ReadOnly));
    const QByteArray text = shader.readAll();
    QVERIFY(text.contains("Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved."));
    QVERIFY(text.contains("Redistribution and use in source and binary forms"));
    QVERIFY(text.contains("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\""));
}

QTEST_GUILESS_MAIN(UpscaleRenderMethodTest)

#include "render_method_test.moc"
