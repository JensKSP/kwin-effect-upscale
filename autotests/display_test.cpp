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

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QElapsedTimer>
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
    void displayShowsTheState();

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
    // GLES requires a float readback for this floating-point framebuffer.
    QImage image(size, QImage::Format_RGBA32FPx4);
    glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_FLOAT, image.bits());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    return glGetError() == GL_NO_ERROR ? image.convertedTo(QImage::Format_RGBA8888) : QImage();
}

void UpscaleDisplayTest::visibilityAndSampling()
{
    UpscaleConfig::setOsd(false);
    UpscaleConfig::setOsdDetection(true);
    UpscaleConfig::setOsdSummary(true);
    UpscaleConfig::setOsdStatistics(false);
    UpscaleConfig::setOsdDeveloper(false);
    // Keep announcement expiry separate from the sampling assertions. Its
    // callback without an EffectsHandler is exercised at the end of this test.
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
    QElapsedTimer composing;
    composing.start();
    display.update(state, nullptr);
    const bool snapshotDue = display.wantsSnapshot(nullptr);
    // Initial font setup can itself exceed the compose interval on a cold
    // machine. The throttle is a time bound, not a guarantee about test speed.
    QVERIFY(!snapshotDue || composing.elapsed() >= 500);
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
    // The counters this exercises are developer information: the view a person
    // turns on carries the frame rate, which no compositor presents here.
    UpscaleConfig::setOsdDeveloper(true);
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
    QTest::qWait(1100);
    QVERIFY(!display.activeFor(nullptr));
    QCOMPARE(paint(display), background);
    // Repainting the same observation must not restart an expired notice.
    display.update(state, nullptr);
    QVERIFY(!display.activeFor(nullptr));
    // Games often put a changing frame counter or scene name in the title.
    // A new caption on the same window must not restart its announcement.
    state.window = QStringLiteral("Observed window — new scene");
    display.update(state, nullptr);
    QVERIFY(!display.activeFor(nullptr));
    QCOMPARE(paint(display), background);
    display.hide();
    QCOMPARE(glGetError(), GLenum(GL_NO_ERROR));
    display.hide();
}

void UpscaleDisplayTest::displayShowsTheState()
{
    // Start from the build's own defaults rather than from whatever an earlier
    // run of this test left in the configuration it writes to.
    const auto settings = []() {
        return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    };
    settings().deleteGroup();
    // Exercise these modes in release builds as well as Debug builds.
    settings().writeEntry("OsdStatistics", true);
    settings().writeEntry("OsdDeveloper", true);
    KSharedConfig::openConfig(QStringLiteral("kwinrc"))->sync();

    UpscaleDisplay display;
    UpscaleConfig::self()->read();
    // This text test has no EffectsHandler to receive the expiry repaint.
    // Visibility expiry is tested separately without delivering that signal.
    UpscaleConfig::setOsdTimeout(60);
    display.reconfigure();
    QVERIFY(display.enabled());
    QVERIFY(display.wantsSnapshot(nullptr));

    UpscaleSnapshot snapshot;
    snapshot.window = QStringLiteral("Tux Racer");
    snapshot.output = QStringLiteral("HDMI-A-1");
    snapshot.selected = true;
    snapshot.scaling = true;
    snapshot.supplied = QSize(1280, 720);
    snapshot.destination = QSize(3840, 2160);
    snapshot.outputScale = 1;
    display.countClientUpdate(nullptr);
    display.countRepaint();
    QElapsedTimer composing;
    composing.start();
    display.update(snapshot, nullptr);
    // Composing again is declined until the interval passes, including time
    // spent loading fonts or waiting to be scheduled on a busy test machine.
    const bool snapshotDue = display.wantsSnapshot(nullptr);
    QVERIFY(!snapshotDue || composing.elapsed() >= 500);

    const QSize targetSize(640, 480);
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
    const UpscaleRectF screen{QPointF(), QSizeF(targetSize)};
    const RenderViewport viewport = captureViewport(screen, 1, target);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    display.paint(target, viewport, screen);
    std::vector<float> pixels(size_t(targetSize.width()) * size_t(targetSize.height()) * 4);
    glReadPixels(0, 0, targetSize.width(), targetSize.height(), GL_RGBA, GL_FLOAT, pixels.data());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    QCOMPARE(glGetError(), GL_NO_ERROR);
    // Something was drawn in the corner the display occupies, and the rest of
    // the output was left to the game.
    const auto red = [&](int x, int y) {
        return pixels[(size_t(y) * size_t(targetSize.width()) + size_t(x)) * 4];
    };
    QVERIFY2(red(40, targetSize.height() - 40) < 0.5F, "the display did not draw where it said it would");
    QVERIFY2(red(targetSize.width() - 3, 3) > 0.9F, "the display covered more than its own area");

    // Rates are reported once a sampling interval has actually passed, with
    // the interval they were measured over. Before that there is nothing
    // measured, and the display says so rather than showing a zero.
    QVERIFY2(display.text().contains(QStringLiteral("Client buffer updates: unknown")), qPrintable(display.text()));
    // The frame rate a person turned this on for is stated as unmeasured
    // rather than as zero until a screen has actually presented something.
    QVERIFY2(display.text().contains(QStringLiteral("Presented: unknown")), qPrintable(display.text()));
    for (int frame = 0; frame < 10; ++frame) {
        display.countClientUpdate(nullptr);
        display.countRepaint();
    }
    QTest::qWait(1100);
    QVERIFY(display.wantsSnapshot(nullptr));
    display.update(snapshot, nullptr);
    QVERIFY2(display.text().contains(QStringLiteral("/s")), qPrintable(display.text()));
    QVERIFY2(display.text().contains(QStringLiteral("s sample")), qPrintable(display.text()));

    // Hiding it gives everything back, and asks for a fresh snapshot when it
    // is shown again rather than drawing a stale one.
    display.hide();
    QVERIFY(display.wantsSnapshot(nullptr));
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    display.paint(target, viewport, screen);
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    QCOMPARE(glGetError(), GL_NO_ERROR);

    // The master switch hides every mode. Nothing is composed, nothing is
    // drawn, and the area it reported is given up with the rest.
    settings().writeEntry("Osd", false);
    KSharedConfig::openConfig(QStringLiteral("kwinrc"))->sync();
    UpscaleConfig::self()->read();
    display.reconfigure();
    QVERIFY(!display.enabled());
    display.update(snapshot, nullptr);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    display.paint(target, viewport, screen);
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    QVERIFY(display.text().isEmpty());
    QCOMPARE(glGetError(), GL_NO_ERROR);
    settings().deleteGroup();
    KSharedConfig::openConfig(QStringLiteral("kwinrc"))->sync();
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
