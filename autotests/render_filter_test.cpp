/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What each filter beside FSR and nearest neighbour draws: what it keeps of a
// picture, the kernel it is named after, and the sharpening that follows it.

#include "picture.h"
#include "render_fixture.h"

#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

using namespace KWin;

Q_DECLARE_METATYPE(KWin::UpscaleFilter)

namespace
{

// The kernels named after their definition, which are compared with it.
constexpr std::array classicFilters{
    std::pair{UpscaleFilter::Bilinear, "bilinear"},
    std::pair{UpscaleFilter::Bicubic, "bicubic"},
    std::pair{UpscaleFilter::Lanczos, "lanczos"},
};

// Every filter but FSR, whose own tests are render_test.cpp's, and nearest
// neighbour, a copy.
constexpr std::array filters{
    std::pair{UpscaleFilter::Bilinear, "bilinear"},
    std::pair{UpscaleFilter::Bicubic, "bicubic"},
    std::pair{UpscaleFilter::Lanczos, "lanczos"},
    std::pair{UpscaleFilter::Gsr, "gsr"},
    std::pair{UpscaleFilter::Nis, "nis"},
};

// The kernels again, in double precision and written from their definitions
// rather than from the shader, so that the shader is compared with what its
// name promises.
double weight(UpscaleFilter filter, double x)
{
    x = std::abs(x);
    switch (filter) {
    case UpscaleFilter::Bicubic:
        if (x < 1) {
            return 1.5 * x * x * x - 2.5 * x * x + 1;
        }
        return x < 2 ? -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2 : 0;
    case UpscaleFilter::Lanczos: {
        if (x < 1e-9) {
            return 1;
        }
        const double angle = std::numbers::pi * x;
        return x < 3 ? std::sin(angle) / angle * std::sin(angle / 3) / (angle / 3) : 0;
    }
    default:
        return std::max(1 - x, 0.0);
    }
}

int support(UpscaleFilter filter)
{
    return filter == UpscaleFilter::Lanczos ? 3 : filter == UpscaleFilter::Bicubic ? 2
                                                                                   : 1;
}

// One channel of the picture at output pixel (x, y), sampled from taps
// clamped to its edge and held within the darkest and brightest of them.
double expectedAt(const std::vector<float> &pixels, const QSize &input, const QSize &output, int x, int y, UpscaleFilter filter)
{
    const double u = (x + 0.5) * input.width() / output.width() - 0.5;
    const double v = (y + 0.5) * input.height() / output.height() - 0.5;
    const int reach = support(filter);
    double sum = 0;
    double total = 0;
    double darkest = 1e20;
    double brightest = -1e20;
    for (int j = int(std::floor(v)) - reach + 1; j <= int(std::floor(v)) + reach; ++j) {
        for (int i = int(std::floor(u)) - reach + 1; i <= int(std::floor(u)) + reach; ++i) {
            const int column = std::clamp(i, 0, input.width() - 1);
            const int row = std::clamp(j, 0, input.height() - 1);
            const double texel = pixels[(size_t(row) * size_t(input.width()) + size_t(column)) * 4];
            const double w = weight(filter, i - u) * weight(filter, j - v);
            sum += w * texel;
            total += w;
            darkest = std::min(darkest, texel);
            brightest = std::max(brightest, texel);
        }
    }
    return std::clamp(sum / total, darkest, brightest);
}

float channelAt(const std::vector<float> &result, const QSize &size, int x, int y, int channel = 0)
{
    return result[(size_t(y) * size_t(size.width()) + size_t(x)) * 4 + size_t(channel)];
}

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

void addFilterColumn(const auto &list)
{
    QTest::addColumn<UpscaleFilter>("filter");
    for (const auto &[filter, name] : list) {
        QTest::newRow(name) << filter;
    }
}

} // namespace

class UpscaleRenderFilterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void keepsAConstantColour_data();
    void keepsAConstantColour();
    void keepsEachCorner_data();
    void keepsEachCorner();
    void followsItsKernel_data();
    void followsItsKernel();
    void reproducesARamp_data();
    void reproducesARamp();
    void staysWithinAnEdge();
    void sharpensAndClips_data();
    void sharpensAndClips();

private:
    UpscaleRenderFixture m_fixture;
};

void UpscaleRenderFilterTest::initTestCase()
{
    QVERIFY(m_fixture.initialize());
}

void UpscaleRenderFilterTest::cleanupTestCase()
{
    m_fixture.release();
}

void UpscaleRenderFilterTest::keepsAConstantColour_data()
{
    QTest::addColumn<UpscaleFilter>("filter");
    QTest::addColumn<int>("transferType");
    QTest::addColumn<double>("value");
    QTest::addColumn<double>("strength");
    for (const auto &[filter, name] : filters) {
        for (const int transfer : {int(TransferFunction::sRGB), int(TransferFunction::linear), int(TransferFunction::PerceptualQuantizer), int(TransferFunction::gamma22)}) {
            for (const double value : {-0.5, 0.0, 0.18, 1.0, 5.0, 40.0}) {
                for (const double strength : {0.0, 1.0}) {
                    const QByteArray row = QByteArray(name) + '-' + QByteArray::number(transfer) + '-' + QByteArray::number(value) + '-' + QByteArray::number(strength);
                    QTest::newRow(row.constData()) << filter << transfer << value << strength;
                }
            }
        }
    }
}

// Weights that do not sum to one, a decode that is not undone, or a working
// encoding left in the output all show as a constant that changes.
void UpscaleRenderFilterTest::keepsAConstantColour()
{
    QFETCH(UpscaleFilter, filter);
    QFETCH(int, transferType);
    QFETCH(double, value);
    QFETCH(double, strength);
    const TransferFunction transfer(static_cast<TransferFunction::Type>(transferType));
    const double expected = upscaleEncodedConstant(value, transfer);
    const QSize inputSize(7, 5);
    const QSize outputSize(13, 9);
    const std::vector<float> result = m_fixture.render(picture(inputSize, [expected](int, int, int) {
        return expected;
    }),
                                                       inputSize, outputSize, transfer, strength, unlimitedRegion(), OutputTransform::Normal, {}, filter);
    QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
    for (size_t index = 0; index < result.size(); ++index) {
        QVERIFY(std::isfinite(result[index]));
        if (index % 4 != 3) {
            QVERIFY2(std::abs(result[index] - expected) <= 0.0005 * std::max(1.0, std::abs(expected)),
                     qPrintable(QStringLiteral("expected %1, got %2").arg(expected, 0, 'g', 12).arg(result[index], 0, 'g', 12)));
        }
    }
}

void UpscaleRenderFilterTest::keepsEachCorner_data()
{
    addFilterColumn(filters);
}

// Four distinct quadrants, at a whole and at a fractional ratio, with and
// without sharpening: a flip or an offset tap in either pass lands a corner
// in the wrong quadrant.
void UpscaleRenderFilterTest::keepsEachCorner()
{
    QFETCH(UpscaleFilter, filter);
    const QSize inputSize(8, 8);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int channel) {
        return channel == 0 ? (x < 4 ? 0.0 : 1.0) : channel == 1 ? (y < 4 ? 0.0 : 1.0)
                                                                 : 0.18;
    });
    const TransferFunction transfer(TransferFunction::gamma22);
    for (const int dimension : {16, 12}) {
        const QSize outputSize(dimension, dimension);
        for (const double strength : {0.0, 1.0}) {
            const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, transfer, strength, unlimitedRegion(),
                                                               OutputTransform::Normal, {}, filter);
            QCOMPARE(result.size(), size_t(dimension * dimension * 4));
            for (const int y : {0, dimension - 1}) {
                for (const int x : {0, dimension - 1}) {
                    QVERIFY(std::abs(channelAt(result, outputSize, x, y, 0) - (x == 0 ? 0.0F : 1.0F)) < 0.001F);
                    QVERIFY(std::abs(channelAt(result, outputSize, x, y, 1) - (y == 0 ? 0.0F : 1.0F)) < 0.001F);
                    QVERIFY(std::abs(channelAt(result, outputSize, x, y, 2) - 0.18F) < 0.001F);
                }
            }
        }
    }
}

void UpscaleRenderFilterTest::followsItsKernel_data()
{
    addFilterColumn(classicFilters);
}

// Every pixel of an irregular picture at a ratio that is no whole number on
// either axis, against the kernel computed here. The destination is encoded,
// so the filter reads the values as given.
void UpscaleRenderFilterTest::followsItsKernel()
{
    QFETCH(UpscaleFilter, filter);
    const QSize inputSize(7, 5);
    const QSize outputSize(12, 9);
    const std::vector<float> input = picture(inputSize, [](int x, int y, int) {
        return 0.1 + 0.8 * std::fmod((x * 37 + y * 91 + x * y * 13) * 0.6180339887, 1.0);
    });
    const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, TransferFunction(TransferFunction::gamma22), 0,
                                                       unlimitedRegion(), OutputTransform::Normal, {}, filter);
    QCOMPARE(result.size(), size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
    for (int y = 0; y < outputSize.height(); ++y) {
        for (int x = 0; x < outputSize.width(); ++x) {
            const double expected = expectedAt(input, inputSize, outputSize, x, y, filter);
            QVERIFY2(std::abs(channelAt(result, outputSize, x, y) - expected) < 2e-4,
                     qPrintable(QStringLiteral("%1,%2: expected %3, got %4").arg(x).arg(y).arg(expected).arg(channelAt(result, outputSize, x, y))));
        }
    }
}

void UpscaleRenderFilterTest::reproducesARamp_data()
{
    QTest::addColumn<UpscaleFilter>("filter");
    QTest::newRow("bilinear") << UpscaleFilter::Bilinear;
    QTest::newRow("bicubic") << UpscaleFilter::Bicubic;
}

// Bilinear and Catmull-Rom interpolation reproduce a straight line exactly,
// wherever their taps do not reach past the picture's edge.
void UpscaleRenderFilterTest::reproducesARamp()
{
    QFETCH(UpscaleFilter, filter);
    const QSize inputSize(8, 2);
    const QSize outputSize(16, 4);
    const std::vector<float> input = picture(inputSize, [](int x, int, int) {
        return x / 7.0;
    });
    const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, TransferFunction(TransferFunction::gamma22), 0,
                                                       unlimitedRegion(), OutputTransform::Normal, {}, filter);
    QCOMPARE(result.size(), size_t(16 * 4 * 4));
    const int reach = support(filter);
    int checked = 0;
    for (int x = 0; x < outputSize.width(); ++x) {
        const double position = (x + 0.5) / 2 - 0.5;
        if (std::floor(position) - reach + 1 < 0 || std::floor(position) + reach > inputSize.width() - 1) {
            continue;
        }
        QVERIFY2(std::abs(channelAt(result, outputSize, x, 1) - position / 7) < 1e-5, qPrintable(QString::number(x)));
        ++checked;
    }
    QVERIFY(checked >= 8);
}

// An edge between two levels. No filter leaves the range between them, and
// bicubic and Lanczos draw it steeper than bilinear, so their limit to that
// range keeps the negative lobes rather than flattening them away. At 2.5
// times, output pixels 9 and 10 lie either side of the edge, 0.2 input pixels
// from it, where bilinear rises by 0.24.
void UpscaleRenderFilterTest::staysWithinAnEdge()
{
    const QSize inputSize(8, 2);
    const QSize outputSize(20, 5);
    const std::vector<float> input = picture(inputSize, [](int x, int, int) {
        return x < 4 ? 0.2 : 0.8;
    });
    std::array<double, classicFilters.size()> rise{};
    for (size_t index = 0; index < classicFilters.size(); ++index) {
        const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, TransferFunction(TransferFunction::gamma22), 0,
                                                           unlimitedRegion(), OutputTransform::Normal, {}, classicFilters[index].first);
        QCOMPARE(result.size(), size_t(20 * 5 * 4));
        for (int x = 0; x < outputSize.width(); ++x) {
            const float value = channelAt(result, outputSize, x, 2);
            QVERIFY2(value >= 0.2F - 1e-6F && value <= 0.8F + 1e-6F, qPrintable(QStringLiteral("%1 at %2: %3").arg(QLatin1String(classicFilters[index].second)).arg(x).arg(value)));
        }
        rise[index] = channelAt(result, outputSize, 10, 2) - channelAt(result, outputSize, 9, 2);
    }
    QVERIFY(std::abs(rise[0] - 0.24) < 1e-5);
    QVERIFY(rise[1] > rise[0]);
    QVERIFY(rise[2] > rise[0]);
}

void UpscaleRenderFilterTest::sharpensAndClips_data()
{
    addFilterColumn(filters);
}

// RCAS follows each classic filter as it follows EASU, GSR and NIS sharpen themselves,
// and a clipped region draws exactly its part of the full frame and nothing
// beside it.
void UpscaleRenderFilterTest::sharpensAndClips()
{
    QFETCH(UpscaleFilter, filter);
    const QSize inputSize(8, 8);
    const QSize outputSize(16, 16);
    const std::vector<float> input = picture(inputSize, [](int x, int, int) {
        return x % 2 == 0 ? 0.15 : 0.65;
    });
    const TransferFunction transfer(TransferFunction::gamma22);
    const auto render = [&](double strength, const UpscaleRegion &region) {
        return m_fixture.render(input, inputSize, outputSize, transfer, strength, region, OutputTransform::Normal, {}, filter);
    };
    const std::vector<float> plain = render(0, unlimitedRegion());
    const std::vector<float> sharpened = render(1, unlimitedRegion());
    QCOMPARE(plain.size(), size_t(16 * 16 * 4));
    QCOMPARE(sharpened.size(), plain.size());
    double difference = 0;
    for (size_t index = 0; index < plain.size(); ++index) {
        QVERIFY(std::isfinite(sharpened[index]));
        difference += std::abs(sharpened[index] - plain[index]);
    }
    QVERIFY(difference > 1.0);
    const std::vector<float> clipped = render(1, UpscaleRegion(UpscaleRect(4, 4, 8, 8)));
    QCOMPARE(clipped.size(), plain.size());
    for (int y = 0; y < outputSize.height(); ++y) {
        for (int x = 0; x < outputSize.width(); ++x) {
            const bool inside = x >= 4 && x < 12 && y >= 4 && y < 12;
            QCOMPARE(channelAt(clipped, outputSize, x, y), inside ? channelAt(sharpened, outputSize, x, y) : -7.0F);
        }
    }
}

QTEST_GUILESS_MAIN(UpscaleRenderFilterTest)

#include "render_filter_test.moc"
