/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "buildtype.h"
#include "eligibility.h"
#include "resolution.h"
#include "scaler.h"
#include "snapshot.h"
#include "upscaleconfig.h"

// The build identity lives outside the plugin folder, because that folder has
// to stay a folder KDE could copy into KWin unchanged, and KWin has nothing
// like it. Out of tree the build adds its include path; copied into KWin the
// header is simply absent and the display reports the build as unknown.
#if __has_include("buildinfo.h")
#include "buildinfo.h"
#define UPSCALE_BUILD_INFO 1
#else
#define UPSCALE_BUILD_INFO 0
#endif

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
    m_display.hide();
}

static bool eligible(EffectWindow *window)
{
    return windowRefusal(window) == UpscaleRefusal::None;
}

void UpscaleEffect::watchWindow(EffectWindow *window)
{
    connect(window, &EffectWindow::windowDamaged, this, [this, window]() {
        if (eligible(window)) {
            // EASU and RCAS read neighbouring pixels. Full-window damage is
            // conservative and follows client commits, never a repaint timer.
            window->addRepaintFull();
            // This is the one event that counts a frame the client produced.
            // A compositor repaint is not the same thing and is counted apart.
            m_display.countClientUpdate();
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
    m_display.reconfigure();
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
    if (candidate()) {
        return true;
    }
    // KWin only calls the paint hooks of effects that say they are active, so
    // an effect that refused every window would never get to say why. The
    // display keeps it in the chain for exactly the case its explanation is
    // needed, and drops out again as soon as it has nothing to show.
    return m_display.enabled() && !effects->isScreenLocked() && displayed() != nullptr;
}

bool UpscaleEffect::blocksDirectScanout() const
{
    // Only eligible content needs composition. KWin still selects presentation
    // mode and refresh timing, including adaptive sync, for composed frames.
    // A visible display is composited content of its own, which is why it is
    // included here and why hiding it gives the requirement back.
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

EffectWindow *UpscaleEffect::displayed() const
{
    if (EffectWindow *scaled = candidate()) {
        return scaled;
    }
    // A refused fullscreen window is exactly the case that needs explaining,
    // so the display follows it. Ordinary desktop windows are left alone.
    EffectWindow *active = effects->activeWindow();
    return active && active->isFullScreen() && !active->isDeleted() ? active : nullptr;
}

UpscaleSnapshot UpscaleEffect::snapshot(EffectWindow *window, const RenderTarget *target) const
{
    UpscaleSnapshot state;
#if UPSCALE_BUILD_INFO
    state.build = UpscaleBuildInfo::describe();
#endif
    state.buildType = upscaleDebugBuild ? i18n("Debug") : i18n("Release");
    state.graphics = usingOpenGLES() ? i18n("OpenGL ES") : i18n("OpenGL");

    UpscaleRefusal refusal = UpscaleRefusal::None;
    const EffectWindow *scaled = candidate(&refusal);
    state.selected = scaled == window;
    state.refusal = state.selected ? m_passRefusal : refusal;
    state.window = window->caption();
    state.application = window->windowClass();
    state.activeWindow = effects->activeWindow() == window;
    state.fullScreen = window->isFullScreen();
    state.blocksScanout = blocksDirectScanout();

    state.enabled = m_enabled;
    state.failed = m_failed;
    state.preset = static_cast<ResolutionPreset>(UpscaleConfig::preset());
    state.percentage = UpscaleConfig::percentage();
    state.sharpening = m_strength;

    if (UpscaleOutput *output = window->screen()) {
        state.output = output->name();
        state.destination = output->pixelSize();
        state.outputScale = output->scale();
        state.desired = desiredResolution({state.destination.width(), state.destination.height()}, state.preset, state.percentage);
    }
    if (SurfaceItem *surface = window->windowItem() ? window->windowItem()->surfaceItem() : nullptr) {
        state.supplied = surface->bufferSize();
        state.format = describeSuppliedFormat(surface);
        state.scaling = m_renderedWindow == window && m_renderedInput == state.supplied;
    }

    // Colour is a property of the frame being painted. Outside a paint pass,
    // as when the settings page asks, it stays unknown rather than guessed.
    if (target) {
        const ColorDescription &colors = targetColors(*target);
        state.transferFunction = int(colors.transferFunction().type);
        state.referenceLuminance = colors.referenceLuminance();
    }
    return state;
}

void UpscaleEffect::paintDisplay(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen)
{
    // A lock screen must not carry a report about what was running behind it,
    // and a display with nothing to show releases what it was holding.
    if (!m_display.enabled() || effects->isScreenLocked()) {
        m_display.hide();
        return;
    }
    EffectWindow *window = displayed();
    if (!window) {
        m_display.hide();
        return;
    }
    if (window->screen() != screen) {
        return;
    }
    m_display.countRepaint();
    if (m_display.wantsSnapshot(window)) {
        m_display.update(snapshot(window, &target), window);
    }
    m_display.paint(target, viewport, screen->geometryF());
}

UpscalePaintResult UpscaleEffect::paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask,
                                              const UpscaleRegion &region, UpscaleOutput *screen)
{
    // The display is drawn after the screen pass, which is after the scaler
    // captured the game's surface. That ordering is what keeps this text out
    // of the captured image and out of the enlargement.
#if UPSCALE_RENDER_DEVICE_API
    if (!effects->paintScreen(target, viewport, mask, region, screen)) {
        return false;
    }
    paintDisplay(target, viewport, screen);
    return true;
#else
    effects->paintScreen(target, viewport, mask, region, screen);
    paintDisplay(target, viewport, screen);
#endif
}

QString UpscaleEffect::status() const
{
    // The settings page may ask about any active window, not only a fullscreen
    // one, so it does not use the display's narrower choice.
    EffectWindow *window = candidate();
    if (!window) {
        window = effects->activeWindow();
    }
    if (!window || !window->screen() || !window->windowItem() || !window->windowItem()->surfaceItem()) {
        return i18n("Inactive: %1", describeRefusal(UpscaleRefusal::NoWindow));
    }
    return upscaleStatusText(snapshot(window, nullptr));
}

int UpscaleEffect::requestedEffectChainPosition() const
{
    return 99;
}

} // namespace KWin
