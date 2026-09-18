/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "display.h"
#include "upscaleconfig.h"

#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"
#include "opengl/glframebuffer.h"
#include "opengl/glvertexbuffer.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace KWin;

class UpscaleDisplayTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void visibilityAndSampling();

private:
    QImage paint(UpscaleDisplay &display);
    std::unique_ptr<EglDisplay> m_display;
    std::shared_ptr<EglContext> m_context;
};

void UpscaleDisplayTest::initTestCase()
{
    const EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#if UPSCALE_RENDER_DEVICE_API
    m_display = EglDisplay::create(display, nullptr);
    QVERIFY(m_display);
    m_context = EglContext::create(m_display.get(), EGL_NO_CONFIG_KHR, {});
#else
    m_display = EglDisplay::create(display);
    QVERIFY(m_display);
    m_context = EglContext::create(m_display.get(), EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT);
#endif
    QVERIFY(m_context);
}

void UpscaleDisplayTest::cleanupTestCase()
{
    m_context.reset();
    m_display.reset();
}

QImage UpscaleDisplayTest::paint(UpscaleDisplay &display)
{
    const QSize size(640, 512);
    std::unique_ptr<GLTexture> texture = allocateFloatTexture(size);
    if (!texture) {
        return {};
    }
    GLFramebuffer framebuffer(texture.get());
    if (!framebuffer.valid()) {
        return {};
    }
    const RenderTarget target(&framebuffer);
    const UpscaleRectF screen(QPointF(), size);
    const RenderViewport viewport = captureViewport(screen, 1, target);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    display.paint(target, viewport, screen);
    QImage image(size, QImage::Format_RGBA8888);
    glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    return image;
}

void UpscaleDisplayTest::visibilityAndSampling()
{
    UpscaleConfig::setOsd(false);
    UpscaleConfig::setOsdDetection(true);
    UpscaleConfig::setOsdSummary(true);
    UpscaleConfig::setOsdStatistics(false);
    UpscaleConfig::setOsdDeveloper(false);
    // This unit test has no EffectsHandler. Keep the repaint-request timer
    // beyond the test's sampling wait; timer delivery belongs to integration.
    UpscaleConfig::setOsdTimeout(60);
    UpscaleDisplay display;
    display.reconfigure();
    QVERIFY(!display.enabled());
    UpscaleConfig::setOsd(true);
    display.reconfigure();
    QVERIFY(display.enabled());
    QVERIFY(display.wantsSnapshot(nullptr));

    UpscaleSnapshot state;
    state.window = QStringLiteral("Observed window");
    state.refusal = UpscaleRefusal::BufferNotSmaller;
    state.supplied = QSize(128, 128);
    state.destination = QSize(128, 128);
    display.update(state, nullptr);
    QVERIFY(!display.wantsSnapshot(nullptr));
    const QImage announcement = paint(display);
    QVERIFY(!announcement.isNull());
    // The plate begins after the TV margin. Readback rows are bottom-up.
    QVERIFY(announcement.pixelColor(33, announcement.height() - 34).red() < 128);
    QCOMPARE(announcement.pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(paint(display), announcement);

    display.hide();
    QVERIFY(display.wantsSnapshot(nullptr));
    const QImage hidden = paint(display);
    QVERIFY(!hidden.isNull());
    QImage background(hidden.size(), hidden.format());
    background.fill(Qt::white);
    QCOMPARE(hidden, background);

    UpscaleConfig::setOsdDetection(false);
    UpscaleConfig::setOsdSummary(false);
    display.reconfigure();
    QVERIFY(!display.enabled());
    UpscaleConfig::setOsdStatistics(true);
    display.reconfigure();
    QVERIFY(display.enabled());
    display.update(state, nullptr);
    const QImage unsampled = paint(display);
    QVERIFY(unsampled != background);
    display.countClientUpdate(nullptr);
    display.countClientUpdate(nullptr);
    display.countRepaint();
    QTest::qSleep(1100);
    QVERIFY(display.wantsSnapshot(nullptr));
    display.update(state, nullptr);
    const QImage sampled = paint(display);
    QVERIFY(sampled != unsampled);
    QCOMPARE(paint(display), sampled);
    // Hiding and reconfiguration start a fresh interval, with unknown rates.
    display.hide();
    QCOMPARE(paint(display), background);
    display.update(state, nullptr);
    QCOMPARE(paint(display), unsampled);
    display.reconfigure();
    display.update(state, nullptr);
    QCOMPARE(paint(display), unsampled);

    UpscaleConfig::setOsdStatistics(false);
    UpscaleConfig::setOsdDeveloper(true);
    display.reconfigure();
    QVERIFY(display.enabled());
    display.update(state, nullptr);
    QVERIFY(paint(display) != sampled);
    UpscaleConfig::setOsdDeveloper(false);
    UpscaleConfig::setOsdDetection(true);
    UpscaleConfig::setOsdTimeout(1);
    display.reconfigure();
    QVERIFY(display.activeFor(nullptr));
    display.update(state, nullptr);
    QVERIFY(display.activeFor(nullptr));
    QTest::qSleep(1100);
    QVERIFY(!display.activeFor(nullptr));
    QCOMPARE(paint(display), background);
    // Repainting the same observation must not restart an expired notice.
    display.update(state, nullptr);
    QVERIFY(!display.activeFor(nullptr));
    display.hide();
    QCOMPARE(glGetError(), GLenum(GL_NO_ERROR));
    display.hide();
}

int main(int argc, char **argv)
{
    QTemporaryDir configuration(QDir::currentPath() + QStringLiteral("/display-test-XXXXXX"));
    if (!configuration.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-upscale-test-bus");
    QGuiApplication application(argc, argv);
    UpscaleDisplayTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "display_test.moc"
