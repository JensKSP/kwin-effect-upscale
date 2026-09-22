/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "core/inputdevice.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "input.h"
#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"
#include "opengl/glframebuffer.h"
#include "opengl/glvertexbuffer.h"
#include "scene/imageitem.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "window.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QStandardPaths>
#include <QTimer>

#include <array>
#include <chrono>
#include <cmath>
#include <functional>

namespace KWin
{

// KWin 6.3's virtual backend requires a DRM device for OpenGL. Run its real
// window lifecycle with QPainter, and supply a deterministic capture renderer
// for the effect's offscreen GL passes. Pixel filtering is tested separately
// with real textures. This boundary does not test client GPU-buffer import.
class CaptureRenderer : public ItemRenderer
{
public:
    void renderBackground(const RenderTarget &, const RenderViewport &, const UpscaleRegion &) override
    {
    }

#if UPSCALE_REGION_API
    void renderItem(const RenderTarget &target, const RenderViewport &viewport, Item *item, int mask,
                    const UpscaleRegion &region, const WindowPaintData &,
                    const std::function<bool(Item *)> &, const std::function<bool(Item *)> &) override
#else
    void renderItem(const RenderTarget &target, const RenderViewport &viewport, Item *item, int mask,
                    const UpscaleRegion &region, const WindowPaintData &) override
#endif
    {
        auto surface = qobject_cast<SurfaceItem *>(item);
        if (!surface || target.size() != surface->bufferSize()
            || viewport.scale() != double(surface->bufferSize().width()) / surface->destinationSize().width()
            || mask != Effect::PAINT_WINDOW_TRANSFORMED || region != unlimitedRegion()) {
            qFatal("Capture did not preserve the original surface's pixel mapping");
        }
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ++captures;
    }

    std::unique_ptr<ImageItem> createImageItem(Item *) override
    {
        return nullptr;
    }

    int captures = 0;
};

// The virtual backend has no input devices, and without a pointer among them
// the seat offers clients no pointer at all, so nothing a test injects would
// ever reach an X window. This device exists so that one does. A test moves it
// by writing a position to a file in the private runtime directory, which
// the driver polls: the test process cannot reach the compositor's input, and
// what then arrives at the X client is the end of the whole path - focus,
// seat, Xwayland - rather than any one piece of it.
class TestPointer : public InputDevice
{
public:
    QString name() const override
    {
        return QStringLiteral("upscale test pointer");
    }
    bool isEnabled() const override
    {
        return true;
    }
    void setEnabled(bool) override
    {
    }
    bool isKeyboard() const override
    {
        return false;
    }
    bool isPointer() const override
    {
        return true;
    }
    bool isTouchpad() const override
    {
        return false;
    }
    bool isTouch() const override
    {
        return false;
    }
    bool isTabletTool() const override
    {
        return false;
    }
    bool isTabletPad() const override
    {
        return false;
    }
    bool isTabletModeSwitch() const override
    {
        return false;
    }
    bool isLidSwitch() const override
    {
        return false;
    }

    void move(const QPointF &position)
    {
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
        Q_EMIT pointerMotionAbsolute(position, now, this);
        // Clients speaking wl_pointer version 5 or later, Xwayland among
        // them, act on a motion only when the frame that closes it arrives.
        Q_EMIT pointerFrame(this);
    }
};

class UpscaleTestDriver : public Effect
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status)
    Q_PROPERTY(QString windows READ windows)
    Q_PROPERTY(QString captured READ captured)
    Q_PROPERTY(bool blocksScanout READ blocksDirectScanout)
    // What a test waits for before judging what a reconfiguration did to an
    // X11 window, rather than a delay that may or may not cover it.
    Q_PROPERTY(bool x11Settled READ x11Settled)

public:
    UpscaleTestDriver()
    {
        const EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        m_display = EglDisplay::create(display);
        if (!m_display) {
            qFatal("Cannot create test EGL display");
        }
        m_context = EglContext::create(m_display.get(), EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT);
        if (!m_context) {
            qFatal("Cannot create test EGL context");
        }
        m_effect = std::make_unique<UpscaleEffect>(&m_renderer);
        m_texture = allocateFloatTexture(QSize(128, 128));
        if (!m_texture) {
            qFatal("Cannot allocate test destination");
        }
        m_framebuffer = std::make_unique<GLFramebuffer>(m_texture.get());
        if (!m_framebuffer->valid()) {
            qFatal("Invalid test framebuffer");
        }
        if (input()) {
            input()->addInputDevice(&m_pointer);
        }
        auto poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, &UpscaleTestDriver::movePointer);
        poll->start(50);
    }

    ~UpscaleTestDriver() override
    {
        if (input()) {
            input()->removeInputDevice(&m_pointer);
        }
        m_context->makeCurrent();
        m_effect.reset();
        m_framebuffer.reset();
        m_texture.reset();
        m_context.reset();
        m_display.reset();
    }

    QString status() const
    {
        return m_effect->status();
    }

    bool x11Settled() const
    {
        return m_effect->x11RequestsSettled();
    }

    QString captured() const
    {
        return m_captured.join(QLatin1Char(','));
    }

    QString windows() const
    {
        QString result;
        QDebug output(&result);
        for (EffectWindow *window : effects->stackingOrder()) {
            const Window *internal = window->window();
            SurfaceItem *surface = window->windowItem()->surfaceItem();
            // Label the geometries. Which type each accessor returns differs
            // between supported KWin versions, and so does whether a
            // fullscreen X11 window reports no border, so a test that anchored
            // on the fields around them would pin KWin's business, not ours.
            output << internal->resourceClass() << internal->resourceName()
                   << internal->isNormalWindow() << internal->noBorder()
                   << "frame" << window->frameGeometry()
                   << "screen" << window->screen()->geometryF();
            if (surface) {
                output << surface->bufferSize() << surface->destinationSize();
            }
        }
        return result;
    }

    void reconfigure(ReconfigureFlags flags) override
    {
        m_captured.clear();
        m_context->makeCurrent();
        // Only a paint pass sees the render target's colour description, so a
        // test cannot reach one through the effect's own settings.
        const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
        const KConfigGroup group(config, QStringLiteral("Effect-upscale-test"));
        m_unsupportedColors = group.readEntry("UnsupportedColors", false);
        if (group.readEntry("ColorsOnly", false)) {
            // Model an output colour change without reconfiguring the effect;
            // otherwise its unconditional reset would hide stale refusals.
            // The same signal under the two names KWin has given it. The
            // plugin already chooses between them; a test driver that named
            // only one would not build wherever the other is current.
#if UPSCALE_REGION_API
            Q_EMIT effects->activeScreen()->blendingColorChanged();
#else
            Q_EMIT effects->activeScreen()->colorDescriptionChanged();
#endif
            effects->addRepaintFull();
            return;
        }
        m_effect->reconfigure(flags);
    }

    void movePointer()
    {
        QFile request(QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-pointer"));
        if (!request.exists() || !request.open(QIODevice::ReadOnly)) {
            return;
        }
        const QList<QByteArray> fields = request.readAll().simplified().split(' ');
        request.close();
        request.remove();
        if (fields.size() == 2) {
            m_pointer.move(QPointF(fields.at(0).toDouble(), fields.at(1).toDouble()));
        }
    }

    int requestedEffectChainPosition() const override
    {
        return m_effect->requestedEffectChainPosition();
    }

    bool blocksDirectScanout() const override
    {
        return m_effect->blocksDirectScanout();
    }

    void paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask,
                     const UpscaleRegion &region, UpscaleOutput *screen) override
    {
        // Continue the real QPainter scene once, with a current offscreen GL
        // target for the injected scaler and the diagnostic display. Keep one
        // streaming-buffer frame around the entire screen, including the OSD.
        // Asked here, before the effect's own paint begins, because that is
        // where KWin asks. Asked from drawWindow instead, inside the effect's
        // paint, it resolved the candidate for the painted output as a side
        // effect, and that hid an effect that asked nothing unless something
        // else resolved it: Auto with the display switched off.
        m_active = m_effect->isActive();
        m_context->makeCurrent();
        GLFramebuffer::pushFramebuffer(m_framebuffer.get());
        GLVertexBuffer::streamingBuffer()->beginFrame();
        m_effect->paintScreen(target, viewport, mask, region, screen);
        GLVertexBuffer::streamingBuffer()->endOfFrame();
        GLFramebuffer::popFramebuffer();
    }

    void drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                    int mask, const UpscaleRegion &region, WindowPaintData &data) override
    {
        m_context->makeCurrent();
        if (m_active) {
            // A zero luminance range is a colour description no transfer
            // function can be decoded from, standing in for an output whose
            // colour handling this effect does not support.
#if UPSCALE_REGION_API
            const auto colors = m_unsupportedColors
                ? ColorDescription::sRGB->withTransferFunction(TransferFunction(TransferFunction::gamma22, 100, 100))
                : ColorDescription::sRGB;
#else
            const ColorDescription colors = m_unsupportedColors
                ? ColorDescription::sRGB.withTransferFunction(TransferFunction(TransferFunction::gamma22, 100, 100))
                : ColorDescription::sRGB;
#endif
            const RenderTarget offscreen(m_framebuffer.get(), colors);
            const RenderViewport offscreenViewport = captureViewport(
                UpscaleRectF(window->screen()->geometryF().topLeft(), QSizeF(128, 128)), 1, offscreen);
            GLFramebuffer::pushFramebuffer(m_framebuffer.get());
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            const int previous = m_renderer.captures;
            m_effect->drawWindow(offscreen, offscreenViewport, window, mask, region, data);
            if (m_renderer.captures > previous) {
                const QString identity = window->window()->resourceClass();
                if (!m_captured.contains(identity)) {
                    m_captured.append(identity);
                }
                std::array<float, 4> pixel;
                glReadPixels(64, 64, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
                if (std::abs(pixel[0] - 1) > 0.001 || std::abs(pixel[1]) > 0.001 || std::abs(pixel[2]) > 0.001 || pixel[3] != 1) {
                    qFatal("Effect did not preserve the captured red pixel");
                }
            }
            GLFramebuffer::popFramebuffer();
            effects->drawWindow(target, viewport, window, mask, region, data);
        } else {
            // The real effect must continue the QPainter chain when disabled,
            // native-sized, transparent, windowed or otherwise ineligible.
            m_effect->drawWindow(target, viewport, window, mask, region, data);
        }
    }

private:
    CaptureRenderer m_renderer;
    bool m_active = false;
    TestPointer m_pointer;
    bool m_unsupportedColors = false;
    std::unique_ptr<EglDisplay> m_display;
    QStringList m_captured;
    std::unique_ptr<EglContext> m_context;
    std::unique_ptr<UpscaleEffect> m_effect;
    std::unique_ptr<GLTexture> m_texture;
    std::unique_ptr<GLFramebuffer> m_framebuffer;
};

KWIN_EFFECT_FACTORY(UpscaleTestDriver, "effect_driver.json")

} // namespace KWin

#include "effect_driver.moc"
