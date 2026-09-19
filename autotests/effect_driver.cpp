/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
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

#include <array>
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

class UpscaleTestDriver : public Effect
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status)
    Q_PROPERTY(QString windows READ windows)
    Q_PROPERTY(QString captured READ captured)
    Q_PROPERTY(bool blocksScanout READ blocksDirectScanout)

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
    }

    ~UpscaleTestDriver() override
    {
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
            output << internal->resourceClass() << internal->resourceName()
                   << internal->isNormalWindow() << internal->noBorder()
                   << window->frameGeometry() << window->screen()->geometryF();
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
        if (m_effect->isActive()) {
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
