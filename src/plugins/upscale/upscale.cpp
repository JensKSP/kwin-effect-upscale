/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "application.h"
#include "buildtype.h"
#include "eligibility.h"
#include "modeoverride.h"
#include "resolution.h"
#include "scaler.h"
#include "settings.h"
#include "snapshot.h"
#include "upscaleconfig.h"
#include "waylandscale.h"
#include "windowidentity.h"
#include "x11resolution.h"

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
#include "wayland/surface.h"
#include "window.h"

#include <KLocalizedString>

#include <QLoggingCategory>
#include <QScopedValueRollback>

#include <array>

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
    // Built before the first reconfiguration, because reconfiguration is what
    // hands it the settings, and before any window is watched, because a game
    // that is already connected can no longer be told anything.
    m_modeOverride = std::make_unique<UpscaleModeOverride>();
    m_x11Resolution = std::make_unique<UpscaleX11Resolution>();
    m_waylandScale = std::make_unique<UpscaleWaylandScale>();
    new UpscaleIdentityService(this);
#if !UPSCALE_RENDER_DEVICE_API
    if (!m_renderer) {
        m_renderer = effects->scene()->renderer();
    }
#endif
#if UPSCALE_BUILD_INFO
    m_build = UpscaleBuildInfo::describe();
    // One complete identity record per successful initialization. Repaints,
    // reconfiguration and opening the settings do not repeat it.
    UpscaleBuildInfo::announce();
#endif
    UpscaleEffect::reconfigure(ReconfigureAll);
    connect(effects, &EffectsHandler::windowAdded, this, &UpscaleEffect::watchWindow);
    connect(effects, &EffectsHandler::screenAdded, this, &UpscaleEffect::watchOutput);
    for (UpscaleOutput *output : effects->screens()) {
        watchOutput(output);
    }
    // The moments a display can stop having anything to describe. KWin calls
    // no paint hook of an inactive effect, so these are the only chances to
    // take what was drawn off the screen; see UpscaleDisplay::hide().
    connect(effects, &EffectsHandler::windowClosed, this, &UpscaleEffect::releaseWhatTheGameLeftBehind);
    connect(effects, &EffectsHandler::windowActivated, this, &UpscaleEffect::releaseWhatTheGameLeftBehind);
    connect(effects, &EffectsHandler::screenLockingChanged, this, &UpscaleEffect::releaseWhatTheGameLeftBehind);
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
    m_waylandScale.reset();
    m_x11Resolution.reset();
    effects->makeOpenGLContextCurrent();
    m_scaler.reset();
    m_display.hide();
}

static bool eligible(EffectWindow *window)
{
    return windowRefusal(window) == UpscaleRefusal::None;
}

// A game that closed, a window that stopped being the one on screen, and a
// locked session all end the display. Hiding it is what asks for the frame
// that erases it; a display that still has its window is left alone, because
// the game's own damage keeps redrawing it.
//
// The same moments are when a game that will never come back stops being a
// reason to hold anything. A game does not have to exit cleanly: one killed
// outright hands nothing back itself, and what this effect kept for it is the
// scaler's buffers, which are the size of the image it was drawing.
void UpscaleEffect::releaseWhatTheGameLeftBehind()
{
    if (m_display.drawn() && (!displayed() || effects->isScreenLocked())) {
        m_display.hide();
    }
    if (candidate()) {
        return;
    }
    // A window that was refused for its colours is remembered by pointer. The
    // pointer is cleared when the window dies; the entry is not.
    m_unsupportedColors.removeAll(nullptr);
    if (m_scaler) {
        // Two textures and their framebuffers at the game's resolution, which
        // is tens of megabytes of video memory at 4K. Nothing on any screen
        // can use them now, and the next game builds its own.
        effects->makeOpenGLContextCurrent();
        m_scaler.reset();
    }
}

void UpscaleEffect::watchWindow(EffectWindow *window)
{
    connect(window, &QObject::destroyed, this, [this, window]() {
        m_renderedInputs.remove(window);
        m_passRefusals.remove(window);
    });
    if (Window *internal = window->window()) {
        // An application ID can arrive after its window does, and an X11
        // client may replace it later. Repaint so that what is shown on
        // screen follows the identity rather than the first guess at it.
        connect(internal, &Window::windowClassChanged, this, [window]() {
            window->addRepaintFull();
        });
    }
    connect(window, &EffectWindow::windowFullScreenChanged, this, [this]() {
        releaseWhatTheGameLeftBehind();
    });
    connect(window, &EffectWindow::windowDamaged, this, [this, window]() {
        if (eligible(window)) {
            // EASU and RCAS read neighbouring pixels. Full-window damage is
            // conservative and follows client commits, never a repaint timer.
            window->addRepaintFull();
        }
        if (m_display.enabled() && !effects->isScreenLocked()) {
            m_display.countClientUpdate(window);
        }
    });
}

void UpscaleEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)
    UpscaleConfig::self()->config()->reparseConfiguration();
    UpscaleConfig::self()->read();
    // Configuration is disk work, so it happens here and never in a frame.
    upscaleReloadApplications();
    // Nothing global is cached here any more. Both controllers resolve what
    // to ask of a program from the profile that claims it, so all they need
    // is to be told the configuration moved underneath them.
    if (m_modeOverride) {
        m_modeOverride->reconfigure();
    }
    m_x11Resolution->reconfigure();
    // Every request this had outstanding was made from configuration that has
    // just moved. Give them all back rather than work out which ones still
    // stand; the next frame asks again for the ones that do.
    if (m_waylandScale) {
        m_waylandScale->releaseAll();
    }
    m_failed = false;
    m_candidateCached = false;
    m_renderedInputs.clear();
    m_unsupportedColors.clear();
    m_passRefusals.clear();
    effects->makeOpenGLContextCurrent();
    // Read once, where a context is guaranteed current: the settings page asks
    // for status outside every paint pass, and an unknown machine's limit is
    // exactly what a report from it has to carry.
    GLint maximumTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
    m_maximumTexture = int(maximumTexture);
    m_scaler.reset();
    m_display.reconfigure();
    // Follow the screen's frames whether or not anything is drawn on it.
    // Watching costs one signal per presented frame; the display's own cost is
    // the drawing and the composition it holds open, which this does not.
    m_display.measure(effects->activeScreen());
    effects->addRepaintFull();
}

bool UpscaleEffect::supported()
{
    if (!effects->isOpenGLCompositing()) {
        return false;
    }
    // The shaders ask for high precision and cannot do without it: medium
    // precision cannot address a 4K pixel grid and would lose HDR detail while
    // sampling. GLSL ES guarantees high precision in a fragment shader only
    // where the implementation offers it, so ask this one instead of assuming
    // that every device KDE runs on can do what this one can.
    if (usingOpenGLES()) {
        std::array<GLint, 2> range = {0, 0};
        GLint precision = 0;
        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range.data(), &precision);
        if (precision == 0) {
            return false;
        }
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
    if (candidate() || autoWaiting()) {
        return true;
    }
    // KWin only calls the paint hooks of effects that say they are active, so
    // an effect that refused every window would never get to say why. The
    // display keeps it in the chain for exactly the case its explanation is
    // needed, and drops out again as soon as it has nothing to show.
    EffectWindow *window = effects->activeWindow();
    return !effects->isScreenLocked() && window && upscalePresentation(window)
        && !window->isDeleted() && m_display.activeFor(window);
}

bool UpscaleEffect::x11RequestsSettled() const
{
    return m_x11Resolution->settled();
}

bool UpscaleEffect::blocksDirectScanout() const
{
    // Only eligible content needs composition. KWin still selects presentation
    // mode and refresh timing, including adaptive sync, for composed frames.
    // A visible display is composited content of its own, which is why it is
    // included here and why hiding it gives the requirement back.
    return isActive();
}

void UpscaleEffect::watchOutput(UpscaleOutput *output)
{
    const auto changed = [this, output]() {
        // Colour refusal is valid only for the output configuration that was
        // painted. Retry on a real change, never by repainting in a loop.
        m_unsupportedColors.removeIf([output](const QPointer<EffectWindow> &window) {
            return !window || window->screen() == output;
        });
        m_candidateCached = false;
        effects->addRepaintFull();
    };
    connect(output, &UpscaleOutput::changed, this, changed);
#if UPSCALE_REGION_API
    connect(output, &UpscaleOutput::blendingColorChanged, this, changed);
#else
    connect(output, &UpscaleOutput::colorDescriptionChanged, this, changed);
#endif
}

EffectWindow *UpscaleEffect::candidate(UpscaleRefusal *refusal, UpscaleOutput *output) const
{
    if (!output && m_inPaint) {
        output = m_paintOutput;
    }
    if (!output) {
        // A status/activation query has no paint output. Prefer the active
        // output, then look for work elsewhere; a refusal on one must never
        // disable a candidate on another.
        EffectWindow *selected = findCandidate(refusal, effects->activeScreen());
        for (UpscaleOutput *screen : effects->screens()) {
            if (!selected && screen != effects->activeScreen()) {
                selected = findCandidate(nullptr, screen);
            }
        }
        return selected;
    }
    if (!m_inPaint) {
        return findCandidate(refusal, output);
    }
    if (!m_candidateCached || m_candidateOutput != output) {
        m_candidate = findCandidate(&m_candidateRefusal, output);
        // Resolve once, here, where the window was chosen. Every value the
        // frame then needs - the sharpening strength, the wish, the display's
        // choices - comes from this one answer, so a frame never asks which
        // layer a setting came from and never reads configuration at all.
        const Window *internal = m_candidate ? m_candidate->window() : nullptr;
        const UpscaleApplication *claimed = upscaleApplicationForWindow(internal);
        m_settings = upscaleResolveSettings(claimed);
        askForSmallerBuffer(output, m_candidate, claimed);
        m_candidateOutput = output;
        m_candidateCached = true;
    }
    if (refusal) {
        *refusal = m_candidateRefusal;
    }
    return m_candidate;
}

EffectWindow *UpscaleEffect::findCandidate(UpscaleRefusal *refusal, UpscaleOutput *output) const
{
    const auto refuse = [refusal](UpscaleRefusal reason) -> EffectWindow * {
        if (refusal) {
            *refusal = reason;
        }
        return nullptr;
    };
    // Whether the effect acts is now a question about one window: the global
    // profile answers for a window no profile claimed, and a profile answers
    // for the game it claimed. windowRefusal() asks it per window, so there is
    // nothing to refuse here before a window has been looked at.
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
        if (window->screen() == output && eligible(window)) {
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
        return refuse(active && active->screen() == output ? windowRefusal(active) : UpscaleRefusal::NoWindow);
    }
    if (m_unsupportedColors.contains(selected)) {
        return refuse(UpscaleRefusal::UnsupportedColors);
    }
    if (refusal) {
        *refusal = UpscaleRefusal::None;
    }
    return selected;
}

// Keep why this window's last pass could not be replaced, and hand the reason
// straight back so the caller can act on it. It describes one frame of one
// window: an effect-wide value would answer a question about this window with
// whatever another output's last pass happened to leave behind.
UpscaleRefusal UpscaleEffect::rememberPassRefusal(EffectWindow *window, UpscaleRefusal refusal)
{
    if (refusal == UpscaleRefusal::None) {
        m_passRefusals.remove(window);
    } else {
        m_passRefusals.insert(window, refusal);
    }
    return refusal;
}

UpscalePaintResult UpscaleEffect::drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                             int mask, const UpscaleRegion &region, WindowPaintData &data)
{
    // Selection is shared by the draws in this paint pass. Check the actual
    // window again before using its surface, which may have been replaced.
    if (m_renderer && eligible(window) && window == candidate(nullptr, m_inPaint ? m_paintOutput : window->screen())) {
        if (!supportsUpscaleColors(targetColors(target))) {
            // Unlike the conditions above, this one follows the output's
            // colour setup and will hold for every frame of this window.
            // Remembering it takes the effect out of the active set, so the
            // output is not held in composition for a frame that will be
            // handed back to KWin anyway. Other windows remain independent.
            m_unsupportedColors.removeAll(nullptr);
            m_unsupportedColors.append(window);
            m_candidateCached = false;
            effects->addRepaintFull();
        } else if (const UpscaleRefusal pass = rememberPassRefusal(window, passRefusal(target, viewport, window, mask, data));
                   pass == UpscaleRefusal::None) {
            if (!m_scaler) {
                m_scaler = std::make_unique<UpscaleScaler>(m_renderer);
                m_failed = !m_scaler->initialize();
            }
#if UPSCALE_REGION_API
            const UpscaleRegion clip = region;
#else
            const UpscaleRegion clip = region == infiniteRegion() ? region : viewport.mapToRenderTarget(region);
#endif
            if (!m_failed && m_scaler->render(target, viewport, window->windowItem()->surfaceItem(), window->frameGeometry(), clip, m_settings.sharpening())) {
                m_renderedInputs.insert(window, window->windowItem()->surfaceItem()->bufferSize());
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
            m_candidateCached = false;
            m_scaler.reset();
            effects->addRepaintFull();
        }
    }
    m_renderedInputs.remove(window);
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
    return active && upscalePresentation(active) && !active->isDeleted() ? active : nullptr;
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
    m_display.measure(screen);
    m_display.countRepaint();
    if (m_display.wantsSnapshot(window)) {
        m_display.update(snapshot(window, &target), window);
    }
    m_display.paint(target, viewport, screen->geometryF());
}

UpscalePaintResult UpscaleEffect::paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask,
                                              const UpscaleRegion &region, UpscaleOutput *screen)
{
    const QScopedValueRollback painting(m_inPaint, true);
    const QScopedValueRollback output(m_paintOutput, screen);
    m_candidateCached = false;
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

QString UpscaleEffect::build() const
{
    return m_build;
}

QString UpscaleEffect::status() const
{
    // The settings page may ask about any active window, not only a fullscreen
    // one, so it does not use the display's narrower choice.
    EffectWindow *window = candidate();
    if (!window) {
        window = effects->activeWindow();
    }
    if (!window) {
        return i18n("Inactive: %1", describeRefusal(UpscaleRefusal::NoWindow));
    }
    // The measurements live in the display, which is what follows one window
    // and one output for long enough to have them. This snapshot is built
    // fresh for the question and has none, so it borrows them rather than
    // reporting a running game with its frame times missing.
    UpscaleSnapshot state = snapshot(window, nullptr);
    m_display.applyMeasurements(state, window, window->screen());
    return upscaleStatusText(state);
}

int UpscaleEffect::requestedEffectChainPosition() const
{
    return 99;
}

} // namespace KWin
