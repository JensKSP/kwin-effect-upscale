/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the filters taken from others draw: Snapdragon GSR 1 and NVIDIA Image
// Scaling, which sharpen as part of their filter, and xBRZ, which redraws the
// edges of pixel art. Neither of the first two reference shaders can run here
// as written, so each is held to what it computes exactly where it finds no
// edge, and to its documented bounds where it does.

#include "nisconfig.h"
#include "picture.h"
#include "render_fixture.h"

#include <QFile>
#include <QRegularExpression>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
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

double luma(const std::vector<float> &pixels, const QSize &size, int x, int y)
{
    return 0.2126 * at(pixels, size, x, y, 0) + 0.7152 * at(pixels, size, x, y, 1) + 0.0722 * at(pixels, size, x, y, 2);
}

// NVIDIA's scaler bank as the shader carries it, read from the resource the
// effect compiles, 64 phases of six taps.
std::vector<double> scalerBank()
{
    QFile file(QStringLiteral(":/opengl/upscale/nis_coefficients.glsl"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    const qsizetype start = text.indexOf(QLatin1String("coefScale[384] = float[384]("));
    const qsizetype end = text.indexOf(QLatin1String(");"), start);
    std::vector<double> bank;
    static const QRegularExpression number(QStringLiteral("-?\\d+\\.\\d+"));
    for (auto match = number.globalMatch(text.mid(start, end - start)); match.hasNext();) {
        bank.push_back(match.next().captured().toDouble());
    }
    return bank;
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
    void nisFiltersLumaWhereItFindsNoEdge();
    void nisSharpensAnEdge();
    void nisKeepsNvidiasConfiguration();
    void xbrzRedrawsADiagonal();
    void carriesQualcommsAndNvidiasNotices();

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

// Where no 3 × 3 neighbourhood passes NVIDIA's edge threshold, every
// directional weight is zero and NVScaler is its six-tap filter on luma, with
// the bilinear colour moved by the difference. This holds the shader's six by
// six window, its phase and its use of the bank to NVScaler's indexing.
//
// The shader takes the phase from an interpolated coordinate in single
// precision. Where the exact position lies on a boundary between two of the 64
// phases, as some output pixel always does at a ratio of whole sizes, it can
// come out on either side, so both are accepted there.
void UpscaleRenderMethodTest::nisFiltersLumaWhereItFindsNoEdge()
{
    const std::vector<double> bank = scalerBank();
    QCOMPARE(bank.size(), size_t(384));
    const auto phases = [](double position) {
        const int base = int(std::floor(position));
        const double scaled = (position - base) * 64;
        std::vector<std::pair<int, int>> found{{base, int(scaled)}};
        const int nearest = int(std::lround(scaled));
        if (std::abs(scaled - nearest) < 1e-3) {
            if (nearest == 0) {
                found.emplace_back(base - 1, 63);
            } else if (nearest == 64) {
                found.emplace_back(base + 1, 0);
            } else {
                found = {{base, nearest - 1}, {base, nearest}};
            }
        }
        return found;
    };
    const QSize inputSize(9, 7);
    const QSize outputSize(13, 10);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int channel) {
        return 0.3 + 0.004 * x + 0.003 * y + 0.001 * ((x * 7 + y * 3) % 5) + 0.1 * channel;
    });
    for (const double strength : {0.0, 1.0}) {
        const std::vector<float> result = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Nis, strength);
        QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
        for (int y = 0; y < outputSize.height(); ++y) {
            for (int x = 0; x < outputSize.width(); ++x) {
                std::array<double, 3> base{};
                for (int channel = 0; channel < 3; ++channel) {
                    base[size_t(channel)] = bilinearAt(input, inputSize, outputSize, x, y, channel);
                }
                const double baseLuma = 0.2126 * base[0] + 0.7152 * base[1] + 0.0722 * base[2];
                double closest = 1;
                for (const auto &[i, phaseX] : phases((x + 0.5) * inputSize.width() / outputSize.width() - 0.5)) {
                    for (const auto &[j, phaseY] : phases((y + 0.5) * inputSize.height() / outputSize.height() - 0.5)) {
                        double filtered = 0;
                        for (int row = 0; row < 6; ++row) {
                            for (int column = 0; column < 6; ++column) {
                                filtered += luma(input, inputSize, i + column - 2, j + row - 2) * bank[size_t(phaseY * 6 + row)] * bank[size_t(phaseX * 6 + column)];
                            }
                        }
                        double worst = 0;
                        for (int channel = 0; channel < 3; ++channel) {
                            worst = std::max(worst, std::abs(at(result, outputSize, x, y, channel) - (base[size_t(channel)] + filtered - baseLuma)));
                        }
                        closest = std::min(closest, worst);
                    }
                }
                QVERIFY2(closest < 2e-5, qPrintable(QStringLiteral("%1,%2: off by %3").arg(x).arg(y).arg(closest)));
            }
        }
    }
}

// An edge is where the directional filters and their unsharp mask work:
// sharpness steepens it, which raises the picture's total variation, and
// nothing leaves zero to one.
void UpscaleRenderMethodTest::nisSharpensAnEdge()
{
    const QSize inputSize(8, 8);
    const QSize outputSize(12, 12);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int) {
        return x + y < 8 ? 0.2 : 0.7;
    });
    const std::vector<float> plain = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Nis, 0);
    const std::vector<float> sharpened = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Nis, 1);
    QCOMPARE(plain.size(), size_t(12 * 12 * 4));
    QCOMPARE(sharpened.size(), plain.size());
    for (size_t index = 0; index < plain.size(); ++index) {
        QVERIFY(std::isfinite(sharpened[index]));
        QVERIFY(plain[index] >= 0.0F && plain[index] <= 1.0F);
        QVERIFY(sharpened[index] >= 0.0F && sharpened[index] <= 1.0F);
    }
    const auto variation = [&outputSize](const std::vector<float> &result) {
        double total = 0;
        for (int y = 0; y < outputSize.height(); ++y) {
            for (int x = 0; x + 1 < outputSize.width(); ++x) {
                total += std::abs(at(result, outputSize, x + 1, y, 1) - at(result, outputSize, x, y, 1));
            }
        }
        return total;
    };
    QVERIFY2(variation(sharpened) > variation(plain), qPrintable(QStringLiteral("%1 %2").arg(variation(plain)).arg(variation(sharpened))));
}

// NVScalerUpdateConfig's values, as NVIDIA computes them, and the one place
// the effect departs from it: a sharpness of zero leaves no unsharp mask.
void UpscaleRenderMethodTest::nisKeepsNvidiasConfiguration()
{
    const UpscaleNisConfig half = upscaleNisConfig(0.5F, UpscaleNisMode::Sdr);
    QCOMPARE(half.detectRatio, 2 * 1127.0F / 1024.0F);
    QCOMPARE(half.detectThreshold, 64.0F / 1024.0F);
    QCOMPARE(half.sharpStrengthMin, 0.4F);
    QCOMPARE(half.sharpStrengthScale, 1.2F);
    QCOMPARE(half.sharpLimitMin, 0.14F);
    QCOMPARE(half.sharpScaleY, 1.0F / (0.9F - 0.45F));
    const UpscaleNisConfig pq = upscaleNisConfig(1.0F, UpscaleNisMode::Pq);
    QCOMPARE(pq.detectThreshold, 32.0F / 1024.0F);
    QCOMPARE(pq.sharpStartY, 0.35F);
    QCOMPARE(pq.sharpStrengthMin, 0.4F + 0.5F * 1.25F * 1.1F);
    const UpscaleNisConfig linear = upscaleNisConfig(0.25F, UpscaleNisMode::Linear);
    QCOMPARE(linear.sharpStartY, 0.3F);
    QCOMPARE(linear.minContrastRatio, 1.5F);
    for (const UpscaleNisMode mode : {UpscaleNisMode::Sdr, UpscaleNisMode::Linear, UpscaleNisMode::Pq}) {
        const UpscaleNisConfig off = upscaleNisConfig(0.0F, mode);
        QCOMPARE(off.sharpStrengthMin, 0.0F);
        QCOMPARE(off.sharpStrengthScale, 0.0F);
    }
}

// A one-pixel diagonal line in pixel art. Nearest neighbour enlarges it into a
// staircase of the two colours alone; xBRZ fills the steps along the diagonal
// with blends between them, keeps the flat areas exactly, and invents no
// colour outside the two. Each output pixel is compared with the input pixel
// it replicates rather than with a nearest-neighbour rendering, which KWin's
// own shader draws at lower precision on OpenGL ES.
void UpscaleRenderMethodTest::xbrzRedrawsADiagonal()
{
    const QSize inputSize(8, 8);
    const QSize outputSize(32, 32);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int) {
        return x == y ? 0.9 : 0.1;
    });
    const std::vector<float> redrawn = render(m_fixture, input, inputSize, outputSize, UpscaleFilter::Xbrz, 0);
    QCOMPARE(redrawn.size(), size_t(32 * 32 * 4));
    std::set<int> levels;
    int blended = 0;
    for (int y = 0; y < outputSize.height(); ++y) {
        for (int x = 0; x < outputSize.width(); ++x) {
            const float value = at(redrawn, outputSize, x, y, 0);
            const float replicated = at(input, inputSize, x / 4, y / 4, 0);
            QVERIFY2(value >= 0.1F - 1e-4F && value <= 0.9F + 1e-4F, qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(value)));
            levels.insert(int(std::lround(value * 1000)));
            blended += std::abs(value - replicated) > 1e-3 ? 1 : 0;
            // Three source pixels from the line, where nothing within reach
            // differs, the pixel is its source pixel's.
            if (std::abs(x / 4 - y / 4) >= 3) {
                QVERIFY2(std::abs(value - replicated) < 1e-5F, qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(value, 0, 'g', 9)));
            }
        }
    }
    QVERIFY2(levels.size() > 2, qPrintable(QString::number(levels.size())));
    QVERIFY(blended > 0);
}

// Qualcomm's and NVIDIA's licences ask for their notices to go with every
// copy, and the shaders reach the plugin as resources, so the resources keep them.
void UpscaleRenderMethodTest::carriesQualcommsAndNvidiasNotices()
{
    QFile gsr(QStringLiteral(":/effects/upscale/shaders/gsr.frag"));
    QVERIFY(gsr.open(QIODevice::ReadOnly));
    const QByteArray qualcomm = gsr.readAll();
    QVERIFY(qualcomm.contains("Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved."));
    QVERIFY(qualcomm.contains("Redistribution and use in source and binary forms"));
    QVERIFY(qualcomm.contains("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\""));
    for (const QString &name : {QStringLiteral(":/effects/upscale/shaders/nis.frag"), QStringLiteral(":/opengl/upscale/nis_coefficients.glsl")}) {
        QFile shader(name);
        QVERIFY2(shader.open(QIODevice::ReadOnly), qPrintable(name));
        const QByteArray nvidia = shader.readAll();
        QVERIFY2(nvidia.contains("Copyright(c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved."), qPrintable(name));
        QVERIFY2(nvidia.contains("Permission is hereby granted, free of charge"), qPrintable(name));
        QVERIFY2(nvidia.contains("THE SOFTWARE IS PROVIDED \"AS IS\""), qPrintable(name));
    }
}

QTEST_GUILESS_MAIN(UpscaleRenderMethodTest)

#include "render_method_test.moc"
