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

#include <QLoggingCategory>

#include <drm_fourcc.h>

Q_LOGGING_CATEGORY(KWIN_UPSCALE, "kwin_effect_upscale", QtWarningMsg)

namespace KWin
{

UpscaleEffect::UpscaleEffect()
    : UpscaleEffect(nullptr)
{
}

UpscaleEffect::UpscaleEffect(ItemRenderer *renderer)
    : m_renderer(renderer)
{
#if !UPSCALE_RENDER_DEVICE_API
    if (!m_renderer) {
        m_renderer = effects->scene()->renderer();
    }
#endif
    UpscaleEffect::reconfigure(ReconfigureAll);
    connect(effects, &EffectsHandler::windowAdded, this, &UpscaleEffect::watchWindow);
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        watchWindow(window);
    }
}

#if UPSCALE_RENDER_DEVICE_API
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
    connect(window, &EffectWindow::windowDamaged, this, [window]() {
        if (eligible(window)) {
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
    m_unsupportedColors.clear();
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
#if UPSCALE_RENDER_DEVICE_API
    // KWin dropped its desktop OpenGL backend along with this API, so version
    // 3.0 means OpenGL ES 3.0 and supplies the GLSL ES 3.00 the shaders need.
    return context->hasVersion(Version(3, 0));
#else
    // GLSL 1.40 arrives with desktop OpenGL 3.1 and GLSL ES 3.00 with ES 3.0.
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
#if UPSCALE_REGION_API
    GraphicsBuffer *buffer = surface->buffer();
#else
    // This call is required, not an optimisation: KWin creates the surface
    // pixmap inside ItemRenderer::renderItem, which runs after the effect
    // chain, so nothing else has made the buffer reachable by the time an
    // effect first looks. Without it the effect is never eligible and never
    // reaches a paint pass that would create the pixmap.
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
#if UPSCALE_RENDER_DEVICE_API
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
    if (selected && selected == m_unsupportedColors) {
        return nullptr;
    }
    m_unsupportedColors.clear();
    return selected;
}

// What the paint pass adds to eligibility. These describe one frame rather
// than the window, so a frame that fails them says nothing about the next.
static bool compatiblePass(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                           int mask, const WindowPaintData &data)
{
    return !(mask & (Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_SCREEN_TRANSFORMED))
        && data.opacity() == 1.0 && data.brightness() == 1.0 && data.saturation() == 1.0
        && data.toMatrix(viewport.scale()).isIdentity()
        && viewport.scale() == window->screen()->scale()
        && target.transform() == OutputTransform::Normal;
}

UpscalePaintResult UpscaleEffect::drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                             int mask, const UpscaleRegion &region, WindowPaintData &data)
{
    // Eligibility is the cheap per-window test; only a window that passes it
    // is worth searching the stacking order for a second, unique candidate.
    if (m_renderer && eligible(window) && window == candidate()) {
        if (!supportsUpscaleColors(targetColors(target))) {
            // Unlike the conditions above, this one follows the output's
            // colour setup and will hold for every frame of this window.
            // Remembering it takes the effect out of the active set, so the
            // output is not held in composition for a frame that will be
            // handed back to KWin anyway. Another candidate clears it.
            m_unsupportedColors = window;
            effects->addRepaintFull();
        } else if (compatiblePass(target, viewport, window, mask, data)) {
            if (!m_scaler) {
                m_scaler = std::make_unique<UpscaleScaler>(m_renderer);
                m_failed = !m_scaler->initialize();
            }
#if UPSCALE_REGION_API
            const UpscaleRegion clip = region;
#else
            const UpscaleRegion clip = region == infiniteRegion() ? region : viewport.mapToRenderTarget(region);
#endif
            if (!m_failed && m_scaler->render(target, viewport, window->windowItem()->surfaceItem(), window->frameGeometry(), clip, m_strength)) {
                m_renderedWindow = window;
                m_renderedInput = window->windowItem()->surfaceItem()->bufferSize();
#if UPSCALE_RENDER_DEVICE_API
                return true;
#else
                return;
#endif
            }
            // A failed allocation or shader must not leave a blank frame or
            // keep blocking scanout. Retry only after a reconfiguration.
            qCWarning(KWIN_UPSCALE, "shader, texture or framebuffer failure; using normal rendering until reconfiguration");
            m_failed = true;
            m_scaler.reset();
            effects->addRepaintFull();
        }
    }
    if (m_renderedWindow == window) {
        m_renderedWindow.clear();
    }
#if UPSCALE_RENDER_DEVICE_API
    return effects->drawWindow(target, viewport, window, mask, region, data);
#else
    effects->drawWindow(target, viewport, window, mask, region, data);
#endif
}

QString UpscaleEffect::status() const
{
    EffectWindow *scaled = candidate();
    EffectWindow *window = scaled ? scaled : effects->activeWindow();
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
    } else if (window == m_unsupportedColors) {
        state = i18n("Inactive: this output's colour handling is not supported.");
    } else if (scaled == window) {
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
