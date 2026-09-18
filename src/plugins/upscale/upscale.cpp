/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "resolution.h"
#include "scaler.h"
#include "upscaleconfig.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "scene/workspacescene.h"

#include <KLocalizedString>

#include <drm_fourcc.h>

namespace KWin
{

UpscaleEffect::UpscaleEffect()
{
#if !UPSCALE_NEW_API
    m_renderer = effects->scene()->renderer();
#endif
    UpscaleEffect::reconfigure(ReconfigureAll);
    connect(effects, &EffectsHandler::windowAdded, this, &UpscaleEffect::watchWindow);
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        watchWindow(window);
    }
}

#if UPSCALE_NEW_API
void UpscaleEffect::prePaintScreen(ScreenPrePaintData &data)
{
    ItemRenderer *renderer = effects->scene()->renderer(data.view->renderDevice());
    if (m_renderer != renderer) {
        m_scaler.reset();
        m_renderer = renderer;
    }
    effects->prePaintScreen(data);
}
#endif

UpscaleEffect::~UpscaleEffect()
{
    effects->makeOpenGLContextCurrent();
    m_scaler.reset();
}

void UpscaleEffect::watchWindow(EffectWindow *window)
{
    connect(window, &EffectWindow::windowDamaged, this, [this, window]() {
        if (window == candidate()) {
            // EASU and RCAS read neighbouring pixels. Full-window damage is
            // conservative and follows client commits, never a repaint timer.
            window->addRepaintFull();
        }
    });
}

void UpscaleEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    UpscaleConfig::self()->read();
    m_enabled = UpscaleConfig::enabled();
    m_strength = sharpeningAmount(UpscaleConfig::sharpening(), UpscaleConfig::strength());
    m_failed = false;
    m_renderedWindow.clear();
    effects->makeOpenGLContextCurrent();
    m_scaler.reset();
    effects->addRepaintFull();
}

bool UpscaleEffect::supported()
{
    if (!effects->isOpenGLCompositing()) {
        return false;
    }
    const auto context = effects->openglContext();
#if UPSCALE_NEW_API
    return context->hasVersion(Version(3, 0));
#else
    return context->hasVersion(context->isOpenGLES() ? Version(3, 0) : Version(3, 1));
#endif
}

bool UpscaleEffect::isActive() const
{
    return candidate() != nullptr;
}

bool UpscaleEffect::blocksDirectScanout() const
{
    // Only eligible content needs composition. KWin still selects presentation
    // mode and refresh timing, including adaptive sync, for composed frames.
    return isActive();
}

static bool rgbBuffer(SurfaceItem *surface)
{
#if UPSCALE_NEW_API
    GraphicsBuffer *buffer = surface->buffer();
#else
    surface->updatePixmap();
    SurfacePixmap *pixmap = surface->pixmap();
    GraphicsBuffer *buffer = pixmap ? pixmap->buffer() : nullptr;
#endif
    if (!buffer) {
        return false;
    }
    const DmaBufAttributes *dmaBuffer = buffer->dmabufAttributes();
    const ShmAttributes *shared = buffer->shmAttributes();
    uint32_t format = 0;
    if (dmaBuffer) {
        format = dmaBuffer->format;
    } else if (shared) {
        format = shared->format;
    }
    switch (format) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR16161616F:
    case DRM_FORMAT_ABGR16161616F:
        return true;
    default:
        return false;
    }
}

bool UpscaleEffect::eligible(EffectWindow *window)
{
    if (!window->isFullScreen() || window->isDeleted() || window->isMinimized()
        || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity() || window->opacity() != 1.0) {
        return false;
    }
    UpscaleOutput *output = window->screen();
    SurfaceItem *surface = window->windowItem()->surfaceItem();
    if (!output || !surface || output->transform() != OutputTransform::Normal
        || window->frameGeometry() != output->geometryF() || !surface->childItems().isEmpty()
        || !surface->transform().isIdentity() || !window->windowItem()->transform().isIdentity()
        || surface->position() != QPointF() || surface->opacity() != 1.0
        || surface->destinationSize() != window->frameGeometry().size()) {
        return false;
    }
    const QSize input = surface->bufferSize();
    const QSize destination = output->pixelSize();
    if (!canUpscale({input.width(), input.height()}, {destination.width(), destination.height()})
        || surface->bufferTransform() != OutputTransform::Normal
        || surface->bufferSourceBox() != UpscaleRectF(QPointF(), input)) {
        return false;
    }
#if UPSCALE_NEW_API
    const bool opaque = surface->opaque().contains(surface->rect());
#else
    const bool opaque = surface->opaque().contains(surface->rect().toAlignedRect());
#endif
    return opaque && rgbBuffer(surface);
}

EffectWindow *UpscaleEffect::candidate() const
{
    if (!m_enabled || m_failed || effects->isScreenLocked() || effects->activeFullScreenEffect()) {
        return nullptr;
    }
    EffectWindow *selected = nullptr;
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        if (eligible(window)) {
            if (selected) {
                return nullptr;
            }
            selected = window;
        }
    }
    return selected;
}

UpscalePaintResult UpscaleEffect::drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                             int mask, const UpscaleRegion &region, WindowPaintData &data)
{
    if (m_renderer && window == candidate() && !(mask & (PAINT_WINDOW_TRANSFORMED | PAINT_SCREEN_TRANSFORMED))
        && data.opacity() == 1.0 && data.brightness() == 1.0 && data.saturation() == 1.0
        && data.toMatrix(viewport.scale()).isIdentity()
        && viewport.scale() == window->screen()->scale()
        && supportsUpscaleColors(targetColors(target))
        && target.transform() == OutputTransform::Normal) {
        if (!m_scaler) {
            m_scaler = std::make_unique<UpscaleScaler>(m_renderer);
            m_failed = !m_scaler->initialize();
        }
#if UPSCALE_NEW_API
        const UpscaleRegion clip = region;
#else
        const UpscaleRegion clip = region == infiniteRegion() ? region : viewport.mapToRenderTarget(region);
#endif
        if (!m_failed && m_scaler->render(target, viewport, window->windowItem()->surfaceItem(), window->frameGeometry(), clip, m_strength)) {
            m_renderedWindow = window;
            m_renderedInput = window->windowItem()->surfaceItem()->bufferSize();
#if UPSCALE_NEW_API
            return true;
#else
            return;
#endif
        }
        // A failed allocation or shader must not leave a blank frame or keep
        // blocking scanout. Retry only after an explicit reconfiguration.
        qWarning("Upscale: shader, texture or framebuffer failure; using normal rendering until reconfiguration");
        m_failed = true;
        m_scaler.reset();
        effects->addRepaintFull();
    }
    if (m_renderedWindow == window) {
        m_renderedWindow.clear();
    }
#if UPSCALE_NEW_API
    return effects->drawWindow(target, viewport, window, mask, region, data);
#else
    effects->drawWindow(target, viewport, window, mask, region, data);
#endif
}

QString UpscaleEffect::status() const
{
    EffectWindow *window = candidate();
    if (!window) {
        window = effects->activeWindow();
    }
    if (!window || !window->screen() || !window->windowItem()->surfaceItem()) {
        return i18n("Inactive: no supplied window buffer.");
    }
    const QSize input = window->windowItem()->surfaceItem()->bufferSize();
    const QSize output = window->screen()->pixelSize();
    const auto preset = static_cast<ResolutionPreset>(UpscaleConfig::preset());
    const UpscaleSize desired = desiredResolution({output.width(), output.height()}, preset, UpscaleConfig::percentage());
    const QString wish = preset == ResolutionPreset::Automatic ? i18n("Automatic (no request)") : i18n("Select %1 × %2 in the game", desired.width, desired.height);
    QString state;
    if (m_failed) {
        state = i18n("Inactive: graphics resource failure; apply settings to retry.");
    } else if (!m_enabled) {
        state = i18n("Inactive: disabled.");
    } else if (candidate() == window) {
        if (m_renderedWindow == window && m_renderedInput == input) {
            state = i18n("FSR 1, sharpening %1%", qRound(m_strength * 100));
        } else {
            state = i18n("Eligible buffer; waiting for a compatible render pass.");
        }
    } else {
        state = i18n("Inactive: requires one opaque, untransformed fullscreen RGB buffer smaller than the output and at least half its size with matching aspect ratio.");
    }
    return i18n("Desired: %1\nSupplied input: %2 × %3\nDestination: %4 × %5\n%6\nHDR follows KWin colour management. Actual VRR presentation is not measured.",
                wish, input.width(), input.height(), output.width(), output.height(), state);
}

int UpscaleEffect::requestedEffectChainPosition() const
{
    return 99;
}

} // namespace KWin
