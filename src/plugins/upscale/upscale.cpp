/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "eligibility.h"
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

static bool eligible(EffectWindow *window)
{
    return windowRefusal(window) == UpscaleRefusal::None;
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
    m_passRefusal = UpscaleRefusal::None;
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

EffectWindow *UpscaleEffect::candidate(UpscaleRefusal *refusal) const
{
    const auto refuse = [refusal](UpscaleRefusal reason) -> EffectWindow * {
        if (refusal) {
            *refusal = reason;
        }
        return nullptr;
    };
    if (!m_enabled) {
        return refuse(UpscaleRefusal::Disabled);
    }
    if (m_failed) {
        return refuse(UpscaleRefusal::ResourceFailure);
    }
    if (effects->isScreenLocked()) {
        return refuse(UpscaleRefusal::ScreenLocked);
    }
    if (effects->activeFullScreenEffect()) {
        return refuse(UpscaleRefusal::OtherFullScreenEffect);
    }
    EffectWindow *selected = nullptr;
    const auto windows = effects->stackingOrder();
    for (EffectWindow *window : windows) {
        if (eligible(window)) {
            if (selected) {
                return refuse(UpscaleRefusal::SeveralCandidates);
            }
            selected = window;
        }
    }
    if (!selected) {
        // Nothing here is eligible, so the interesting answer is why the
        // window the user is looking at is not. Asking costs one more pass
        // over the conditions and happens only for a caller that wants it.
        EffectWindow *active = effects->activeWindow();
        return refuse(active ? windowRefusal(active) : UpscaleRefusal::NoWindow);
    }
    if (selected == m_unsupportedColors) {
        return refuse(UpscaleRefusal::UnsupportedColors);
    }
    m_unsupportedColors.clear();
    if (refusal) {
        *refusal = UpscaleRefusal::None;
    }
    return selected;
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
        } else if (m_passRefusal = passRefusal(target, viewport, window, mask, data); m_passRefusal == UpscaleRefusal::None) {
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
    UpscaleRefusal refusal = UpscaleRefusal::None;
    EffectWindow *scaled = candidate(&refusal);
    EffectWindow *window = scaled ? scaled : effects->activeWindow();
    if (!window || !window->screen() || !window->windowItem() || !window->windowItem()->surfaceItem()) {
        return i18n("Inactive: %1", describeRefusal(UpscaleRefusal::NoWindow));
    }
    SurfaceItem *surface = window->windowItem()->surfaceItem();
    const QSize input = surface->bufferSize();
    const QSize output = window->screen()->pixelSize();
    const auto preset = static_cast<ResolutionPreset>(UpscaleConfig::preset());
    const UpscaleSize desired = desiredResolution({output.width(), output.height()}, preset, UpscaleConfig::percentage());
    const QString wish = preset == ResolutionPreset::Automatic ? i18n("Automatic (no request)") : i18n("Select %1 × %2 in the game", desired.width, desired.height);
    QString state;
    if (scaled == window) {
        if (m_renderedWindow == window && m_renderedInput == input) {
            state = i18n("FSR 1, sharpening %1%", qRound(m_strength * 100));
        } else if (m_passRefusal != UpscaleRefusal::None) {
            state = i18n("Eligible buffer; the last frame was not scaled because %1", describeRefusal(m_passRefusal));
        } else {
            state = i18n("Eligible buffer; waiting for a compatible render pass.");
        }
    } else {
        // The refusal belongs to this window: either it is the candidate the
        // effect turned down, or there is no candidate and the reason was
        // taken from the active window, which is the one reported here.
        state = i18n("Inactive: %1", describeRefusal(refusal));
        if (refusal == UpscaleRefusal::UnsupportedBufferFormat) {
            state += QLatin1Char(' ') + i18n("Supplied format: %1.", describeSuppliedFormat(surface));
        }
    }
    return i18n("Desired: %1\nSupplied input: %2 × %3\nDestination: %4 × %5\n%6\nHDR follows KWin colour management. Actual VRR presentation is not measured.",
                wish, input.width(), input.height(), output.width(), output.height(), state);
}

int UpscaleEffect::requestedEffectChainPosition() const
{
    return 99;
}

} // namespace KWin
