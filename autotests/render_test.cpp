/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "scaler.h"

#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"
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

private:
    std::vector<float> render(const std::vector<float> &pixels, const QSize &inputSize, const QSize &outputSize,
                              TransferFunction transfer, double strength, const UpscaleRegion &region = unlimitedRegion());

    std::unique_ptr<EglDisplay> m_display;
    std::shared_ptr<EglContext> m_context;
    std::unique_ptr<UpscaleScaler> m_scaler;
};

void UpscaleRenderTest::initTestCase()
{
    // A headless EGL display exercises KWin's real shader manager and textures
    // under Mesa. It does not establish compositor lifecycle or hardware VRR.
    const EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#if UPSCALE_NEW_API
    m_display = EglDisplay::create(display, nullptr);
#else
    m_display = EglDisplay::create(display);
#endif
    QVERIFY(m_display);
#if UPSCALE_NEW_API
    m_context = EglContext::create(m_display.get(), EGL_NO_CONFIG_KHR, {});
#else
    m_context = EglContext::create(m_display.get(), EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT);
#endif
    QVERIFY(m_context);
    qInfo() << "OpenGL:" << reinterpret_cast<const char *>(glGetString(GL_VERSION));
    m_scaler = std::make_unique<UpscaleScaler>(nullptr);
    QVERIFY(m_scaler->initialize());
}

void UpscaleRenderTest::cleanupTestCase()
{
    m_scaler.reset();
    m_context.reset();
    m_display.reset();
}

std::vector<float> UpscaleRenderTest::render(const std::vector<float> &pixels, const QSize &inputSize, const QSize &outputSize,
                                             TransferFunction transfer, double strength, const UpscaleRegion &region)
{
    std::unique_ptr<GLTexture> input = allocateFloatTexture(inputSize);
    std::unique_ptr<GLTexture> output = allocateFloatTexture(outputSize);
    if (!input || !output) {
        return {};
    }
    input->bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inputSize.width(), inputSize.height(), GL_RGBA, GL_FLOAT, pixels.data());
    input->unbind();
    GLFramebuffer framebuffer(output.get());
    if (!framebuffer.valid()) {
        return {};
    }
#if UPSCALE_NEW_API
    const auto colors = ColorDescription::sRGB->withTransferFunction(transfer);
#else
    const auto colors = ColorDescription::sRGB.withTransferFunction(transfer);
#endif
    const RenderTarget target(&framebuffer, colors);
    const UpscaleRectF rectangle(QPointF(), outputSize);
    const RenderViewport viewport = captureViewport(rectangle, 1, target);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    glClearColor(-7, -7, -7, -7);
    glClear(GL_COLOR_BUFFER_BIT);
    const bool success = m_scaler->renderTexture(target, viewport, input.get(), rectangle, region, strength);
    std::vector<float> result(size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
    glReadPixels(0, 0, outputSize.width(), outputSize.height(), GL_RGBA, GL_FLOAT, result.data());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    if (!success || glGetError() != GL_NO_ERROR) {
        return {};
    }
    return result;
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
#if UPSCALE_NEW_API
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
    const std::vector<float> result = render(input, inputSize, outputSize, transfer, strength);
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
#if UPSCALE_NEW_API
    const double reference = ColorDescription::sRGB->referenceLuminance();
#else
    const double reference = ColorDescription::sRGB.referenceLuminance();
#endif
    const TransferFunction transfer(TransferFunction::linear, 0, reference);
    for (const int dimension : {16, 12, 16}) {
        for (const double strength : {0.0, 1.0}) {
            const std::vector<float> result = render(input, inputSize, QSize(dimension, dimension), transfer, strength);
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
    const std::vector<float> plain = render(input, QSize(8, 8), QSize(16, 16), transfer, 0);
    const std::vector<float> sharpened = render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
    QCOMPARE(plain.size(), size_t(16 * 16 * 4));
    QCOMPARE(sharpened.size(), plain.size());
    double difference = 0;
    for (size_t index = 0; index < plain.size(); ++index) {
        QVERIFY(std::isfinite(sharpened[index]));
        difference += std::abs(sharpened[index] - plain[index]);
    }
    QVERIFY(difference > 1.0);
    const std::vector<float> clipped = render(input, QSize(8, 8), QSize(16, 16), transfer, 1, UpscaleRegion(UpscaleRect(4, 4, 8, 8)));
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
    const std::vector<float> expected = render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
    QCOMPARE(expected.size(), size_t(16 * 16 * 4));
    // A preceding effect can leave a scissor active. It must not clip the
    // intermediate EASU pass, and the caller's rectangle must be restored.
    glScissor(1, 2, 3, 4);
    glEnable(GL_SCISSOR_TEST);
    const std::vector<float> actual = render(input, QSize(8, 8), QSize(16, 16), transfer, 1);
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
    const bool rendered = m_scaler->renderTexture(target, viewport, input.get(), UpscaleRectF(0, 0, maximumSize + 1, 16), unlimitedRegion(), 1);
    GLFramebuffer::popFramebuffer();
    QVERIFY(!rendered);
    QCOMPARE(glGetError(), GLenum(GL_NO_ERROR));
    // The rejected allocation must not poison a subsequent supported render.
    const std::vector<float> pixels(8 * 8 * 4, 1);
    QCOMPARE(render(pixels, QSize(8, 8), QSize(16, 16), TransferFunction(TransferFunction::gamma22), 1).size(), size_t(16 * 16 * 4));
}

QTEST_GUILESS_MAIN(UpscaleRenderTest)

#include "render_test.moc"
