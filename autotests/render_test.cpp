/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "overlay.h"
#include "render_fixture.h"

#include "opengl/glframebuffer.h"
#include "opengl/gltexture.h"
#include "opengl/glvertexbuffer.h"

#include <QTest>

#include <array>
#include <cmath>
#include <vector>

using namespace KWin;

class UpscaleRenderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void constantColors_data();
    void constantColors();
    void orientationAndResize();
    void sharpeningAndClipping();
    void preservesScissorState();
    void rejectsOversizedIntermediate();
    void overlayPlacement_data();
    void overlayPlacement();
    void overlayEmphasis();

private:
    UpscaleRenderFixture m_fixture;
};

void UpscaleRenderTest::initTestCase()
{
    QVERIFY(m_fixture.initialize());
}

void UpscaleRenderTest::cleanupTestCase()
{
    m_fixture.release();
}

void UpscaleRenderTest::constantColors_data()
{
    QTest::addColumn<double>("value");
    QTest::addColumn<int>("transferType");
    QTest::addColumn<double>("strength");
    for (const double strength : {0.0, 0.25, 1.0}) {
        for (const int transfer : {int(TransferFunction::sRGB), int(TransferFunction::linear), int(TransferFunction::PerceptualQuantizer), int(TransferFunction::gamma22)}) {
            for (const double value : {-0.5, 0.0, 0.001, 0.005, 0.18, 1.0, 5.0, 40.0}) {
                const QByteArray name = QByteArray::number(transfer) + '-' + QByteArray::number(value) + '-' + QByteArray::number(strength);
                QTest::newRow(name.constData()) << value << transfer << strength;
            }
        }
    }
}

void UpscaleRenderTest::constantColors()
{
    QFETCH(double, value);
    QFETCH(int, transferType);
    QFETCH(double, strength);
    const QSize inputSize(8, 8);
    const QSize outputSize(16, 16);
    const TransferFunction transfer(static_cast<TransferFunction::Type>(transferType));
#if UPSCALE_REGION_API
    const double reference = ColorDescription::sRGB->referenceLuminance();
#else
    const double reference = ColorDescription::sRGB.referenceLuminance();
#endif
    double expected = transfer.nitsToEncoded(QVector3D(float(value * reference), 0, 0)).x();
    const double normalized = (value * reference - transfer.minLuminance) / (transfer.maxLuminance - transfer.minLuminance);
    if (transfer.type == TransferFunction::sRGB && normalized < 0.0031308) {
        // KWin 6.3's CPU helper divides here; its shader and the sRGB inverse
        // EOTF multiply by 12.92. Test the shader's specified transfer curve.
        expected = std::max(12.92 * normalized, 0.0);
    }
    std::vector<float> input(8 * 8 * 4, float(expected));
    for (size_t index = 3; index < input.size(); index += 4) {
        input[index] = 1;
    }
    const std::vector<float> result = m_fixture.render(input, inputSize, outputSize, transfer, strength);
    QCOMPARE(result.size(), size_t(16 * 16 * 4));
    for (size_t index = 0; index < result.size(); ++index) {
        QVERIFY(std::isfinite(result[index]));
        if (index % 4 == 3) {
            QCOMPARE(result[index], 1.0F);
        } else {
            QVERIFY2(std::abs(result[index] - expected) <= 0.0005 * std::max(1.0, std::abs(expected)),
                     qPrintable(QStringLiteral("expected %1, got %2").arg(expected, 0, 'g', 12).arg(result[index], 0, 'g', 12)));
        }
    }
}

void UpscaleRenderTest::orientationAndResize()
{
    // Storage coordinates are bottom-up for both fixtures and readback. Four
    // distinct corners catch vertical flips in either the EASU or RCAS pass.
    const QSize inputSize(8, 8);
    std::vector<float> input(8 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const size_t pixel = size_t(y * 8 + x) * 4;
            input[pixel] = x < 4 ? 0.0F : 1.0F;
            input[pixel + 1] = y < 4 ? 0.0F : 1.0F;
            input[pixel + 2] = 0.18F;
            input[pixel + 3] = 1;
        }
    }
#if UPSCALE_REGION_API
    const double reference = ColorDescription::sRGB->referenceLuminance();
#else
    const double reference = ColorDescription::sRGB.referenceLuminance();
#endif
    const TransferFunction transfer(TransferFunction::linear, 0, reference);
    for (const int dimension : {16, 12, 16}) {
        for (const double strength : {0.0, 1.0}) {
            const std::vector<float> result = m_fixture.render(input, inputSize, QSize(dimension, dimension), transfer, strength);
            QCOMPARE(result.size(), size_t(dimension * dimension * 4));
            for (const int y : {0, dimension - 1}) {
                for (const int x : {0, dimension - 1}) {
                    const size_t pixel = size_t(y * dimension + x) * 4;
                    QVERIFY(std::abs(result[pixel] - (x == 0 ? 0.0F : 1.0F)) < 0.001F);
                    QVERIFY(std::abs(result[pixel + 1] - (y == 0 ? 0.0F : 1.0F)) < 0.001F);
                    QVERIFY(std::abs(result[pixel + 2] - 0.18F) < 0.001F);
                }
            }
        }
    }
}

void UpscaleRenderTest::sharpeningAndClipping()
{
    std::vector<float> input(8 * 8 * 4, 1);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const float value = x % 2 == 0 ? 0.15F : 0.65F;
            for (int channel = 0; channel < 3; ++channel) {
                input[size_t((y * 8 + x) * 4 + channel)] = value;
            }
        }
    }
    const TransferFunction transfer(TransferFunction::gamma22);
    const std::vector<float> plain = m_fixture.render(input, QSize(8, 8), QSize(16, 16), transfer, 0);
    const std::vector<float> sharpened = m_fixture.render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
    QCOMPARE(plain.size(), size_t(16 * 16 * 4));
    QCOMPARE(sharpened.size(), plain.size());
    double difference = 0;
    for (size_t index = 0; index < plain.size(); ++index) {
        QVERIFY(std::isfinite(sharpened[index]));
        difference += std::abs(sharpened[index] - plain[index]);
    }
    QVERIFY(difference > 1.0);
    const std::vector<float> clipped = m_fixture.render(input, QSize(8, 8), QSize(16, 16), transfer, 1, UpscaleRegion(UpscaleRect(4, 4, 8, 8)));
    QCOMPARE(clipped.size(), plain.size());
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const size_t pixel = size_t(y * 16 + x) * 4;
            const bool inside = x >= 4 && x < 12 && y >= 4 && y < 12;
            QCOMPARE(clipped[pixel], inside ? sharpened[pixel] : -7.0F);
        }
    }
}

void UpscaleRenderTest::preservesScissorState()
{
    const std::vector<float> input(8 * 8 * 4, 1);
    const TransferFunction transfer(TransferFunction::gamma22);
    const std::vector<float> expected = m_fixture.render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
    QCOMPARE(expected.size(), size_t(16 * 16 * 4));
    // A preceding effect can leave a scissor active. It must not clip the
    // intermediate EASU pass, and the caller's rectangle must be restored.
    glScissor(1, 2, 3, 4);
    glEnable(GL_SCISSOR_TEST);
    const std::vector<float> actual = m_fixture.render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
    const bool enabled = glIsEnabled(GL_SCISSOR_TEST);
    std::array<GLint, 4> rectangle;
    glGetIntegerv(GL_SCISSOR_BOX, rectangle.data());
    glDisable(GL_SCISSOR_TEST);
    QVERIFY(enabled);
    QCOMPARE(rectangle, (std::array<GLint, 4>{1, 2, 3, 4}));
    QCOMPARE(actual, expected);
}

void UpscaleRenderTest::rejectsOversizedIntermediate()
{
    std::unique_ptr<GLTexture> input = allocateFloatTexture(QSize(8, 8));
    std::unique_ptr<GLTexture> output = allocateFloatTexture(QSize(16, 16));
    QVERIFY(input);
    QVERIFY(output);
    GLFramebuffer framebuffer(output.get());
    QVERIFY(framebuffer.valid());
    const RenderTarget target(&framebuffer);
    const RenderViewport viewport = captureViewport(UpscaleRectF(0, 0, 16, 16), 1, target);
    GLint maximumSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    const bool rendered = m_fixture.m_scaler->renderTexture(target, viewport, input.get(), UpscaleRectF(0, 0, maximumSize + 1, 16), unlimitedRegion(), 1);
    GLFramebuffer::popFramebuffer();
    QVERIFY(!rendered);
    QCOMPARE(glGetError(), GLenum(GL_NO_ERROR));
    // The rejected allocation must not poison a subsequent supported render.
    const std::vector<float> pixels(8 * 8 * 4, 1);
    QCOMPARE(m_fixture.render(pixels, QSize(8, 8), QSize(16, 16), TransferFunction(TransferFunction::gamma22), 1).size(), size_t(16 * 16 * 4));
}

// The overlay measures and draws text, which needs a font database, so this
// test needs a GUI application even though it renders offscreen.
void UpscaleRenderTest::overlayPlacement_data()
{
    QTest::addColumn<bool>("blending");
    QTest::newRow("blending-disabled") << false;
    QTest::newRow("blending-enabled") << true;
}

void UpscaleRenderTest::overlayPlacement()
{
    QFETCH(bool, blending);
    const QSize targetSize(320, 160);
    std::unique_ptr<GLTexture> output = allocateFloatTexture(targetSize);
    QVERIFY(output);
    GLFramebuffer framebuffer(output.get());
    QVERIFY(framebuffer.valid());
#if UPSCALE_REGION_API
    const auto colors = ColorDescription::sRGB;
#else
    const auto &colors = ColorDescription::sRGB;
#endif
    const RenderTarget target(&framebuffer, colors);
    // A pass that renders an output which does not start at the origin. The
    // text has to land at that output's own corner, which is what a second
    // monitor to the right of the first one would ask for.
    const QPointF origin(200, 100);
    const RenderViewport viewport = captureViewport(UpscaleRectF(origin, QSizeF(targetSize)), 1, target);
    UpscaleOverlay overlay;
    overlay.setText(QStringLiteral("Upscale"), 1);
    QVERIFY(!overlay.isEmpty());
    QVERIFY(overlay.size().width() > 0);
    QVERIFY(overlay.size().height() > 0);
    QVERIFY(overlay.size().width() < targetSize.width());
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (blending) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const bool painted = overlay.paint(target, viewport, origin);
    QCOMPARE(bool(glIsEnabled(GL_BLEND)), blending);
    GLint factor;
    glGetIntegerv(GL_BLEND_SRC_RGB, &factor);
    QCOMPARE(factor, GLint(GL_SRC_ALPHA));
    glGetIntegerv(GL_BLEND_DST_RGB, &factor);
    QCOMPARE(factor, GLint(GL_ONE_MINUS_DST_ALPHA));
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &factor);
    QCOMPARE(factor, GLint(GL_DST_ALPHA));
    glGetIntegerv(GL_BLEND_DST_ALPHA, &factor);
    QCOMPARE(factor, GLint(GL_ONE_MINUS_SRC_ALPHA));
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    std::vector<float> pixels(size_t(targetSize.width()) * size_t(targetSize.height()) * 4);
    glReadPixels(0, 0, targetSize.width(), targetSize.height(), GL_RGBA, GL_FLOAT, pixels.data());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    QVERIFY(painted);
    QCOMPARE(glGetError(), GL_NO_ERROR);
    const auto red = [&](int x, int y) {
        return pixels[(size_t(y) * size_t(targetSize.width()) + size_t(x)) * 4];
    };
    // Readback is bottom-up, so the last row is the top of the screen, where
    // the plate is. It is dark; the opposite corner keeps the background.
    QVERIFY2(red(3, targetSize.height() - 4) < 0.5F, "the overlay did not cover the top left corner of the pass");
    QVERIFY2(red(targetSize.width() - 3, 3) > 0.9F, "the overlay covered more than its own area");
    // Hiding it has to give the texture back rather than keep it for later.
    overlay.release();
    QVERIFY(overlay.isEmpty());
    QCOMPARE(overlay.size(), QSizeF());
}

void UpscaleRenderTest::overlayEmphasis()
{
    UpscaleOverlay overlay;
    const QString text = QStringLiteral("60 FPS   16.7 ms");
    for (const double scale : {1.0, 1.5, 2.0}) {
        overlay.setText(text, scale);
        const QSizeF normal = overlay.size();
        // Only emphasis changes: unchanged text must still be laid out at
        // the new size, while output scaling remains in logical coordinates.
        overlay.setText(text, scale, 1.6);
        QVERIFY(overlay.size().width() > normal.width() * 1.4);
        QVERIFY(overlay.size().height() > normal.height() * 1.4);
        QVERIFY(overlay.size().width() < normal.width() * 1.8);
        QVERIFY(overlay.size().height() < normal.height() * 1.8);
        QCOMPARE(overlay.text(), text);
        overlay.setText(text, scale);
        QCOMPARE(overlay.size(), normal);
    }
}

// The overlay measures and draws text, which needs a font database, so this
// test needs a GUI application even though it renders offscreen.
QTEST_MAIN(UpscaleRenderTest)

#include "render_test.moc"
