/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// One consistent reading of what the effect and the window are doing right
// now, taken in one place so that no two lines of a report can describe two
// different moments. It lives apart from the effect's paint path because it
// only reads state: nothing here draws, decides or changes anything, and
// keeping it here is what keeps upscale.cpp within the file-size budget.

#include "upscale.h"

#include "application.h"
#include "buildtype.h"
#include "eligibility.h"
#include "matching.h"
#include "modeoverride.h"
#include "resolution.h"
#include "settings.h"
#include "snapshot.h"
#include "upscaleconfig.h"
#include "waylandscale.h"
#include "windowidentity.h"
#include "x11resolution.h"

#include "config-kwin.h"
#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "wayland/clientconnection.h"
#include "wayland/surface.h"
#include "window.h"

#include <KLocalizedString>

namespace KWin
{

// Whether the size a method that tells a scale told the program is the step
// the wish as it stands reaches, rather than a wish that has moved on since.
// The advertised mode and Auto tell the wish itself.
static bool nearestReachable(const UpscaleSnapshot &state)
{
    if (!state.advertised.isValid() || state.method == UpscaleMethod::AdvertisedMode || state.method == UpscaleMethod::Auto) {
        return false;
    }
    const UpscaleSize pixels{state.destination.width(), state.destination.height()};
    const int step = reachableScale(pixels, state.outputScale, state.preset, state.percentage);
    const UpscaleSize nearest = step > 0 ? scaledRequest(pixels, state.outputScale, step) : UpscaleSize{};
    return QSize(nearest.width, nearest.height) == state.advertised;
}

// How the picture is sampled, and where it goes when it goes anywhere.
static void describePicture(UpscaleSnapshot &state, EffectWindow *window, const UpscaleSettings &settings)
{
    state.sharpening = settings.sharpening();
    state.filter = settings.filter();
    if (const UpscalePicture placed = upscalePictureOf(window); placed.sizing == UpscaleSizing::Supported) {
        state.picture = QRect(placed.x, placed.y, placed.width, placed.height);
        state.factor = placed.factor;
    }
}

// The output a window is on, and what the settings wish for on it.
static void describeOutput(UpscaleSnapshot &state, UpscaleOutput *output)
{
    state.output = output->name();
    state.destination = output->pixelSize();
    state.outputScale = output->scale();
    state.outputArea = output->geometryF();
    state.desired = desiredResolution({state.destination.width(), state.destination.height()}, state.preset, state.percentage);
    state.nearestReachable = nearestReachable(state);
}

UpscaleSnapshot UpscaleEffect::snapshot(EffectWindow *window, const RenderTarget *target) const
{
    UpscaleSnapshot state;
    state.build = m_build;
    state.buildType = upscaleDebugBuild ? i18n("Debug") : i18n("Release");
    state.graphics = usingOpenGLES() ? i18n("OpenGL ES") : i18n("OpenGL");

    UpscaleRefusal refusal = UpscaleRefusal::None;
    const EffectWindow *scaled = candidate(&refusal, window->screen());
    state.selected = scaled == window;
    // The pass refusal describes one frame of one window. Reading an
    // effect-wide value here would answer a question about this window with
    // whatever another output's last pass happened to leave behind.
    state.refusal = state.selected ? m_passRefusals.value(window, UpscaleRefusal::None) : refusal;
    state.window = window->caption();
    state.application = window->windowClass();
    describeApplication(state, window->window(), upscalePresentationOf(window));
    state.activeWindow = effects->activeWindow() == window;
    state.fullScreen = window->isFullScreen();
    state.blocksScanout = blocksDirectScanout();

    state.failed = m_failed;
    state.maximumTexture = m_maximumTexture;
    // What applies to this window, profile over global, rather than the global
    // layer alone. A report naming the global value would describe a window
    // other than the one being looked at, which is the whole point of a report.
    const UpscaleSettings settings = upscaleResolveSettings(upscaleApplicationForWindow(window->window()));
    state.enabled = settings.acts();
    state.preset = settings.resolution();
    state.percentage = settings.value(UpscaleSetting::Percentage);
    describePicture(state, window, settings);

    if (UpscaleOutput *output = window->screen()) {
        describeOutput(state, output);
    }
    // Which window system the client speaks decides which requests can reach
    // it at all, so it is recorded for every window, refused or not. Both are
    // constant for a window's lifetime.
    state.windowArea = window->frameGeometry();
    if (window->isWaylandClient()) {
        state.windowSystem = UpscaleWindowSystem::Wayland;
    } else if (window->isX11Client()) {
        state.windowSystem = UpscaleWindowSystem::X11;
    }
    if (SurfaceItem *surface = window->windowItem() ? window->windowItem()->surfaceItem() : nullptr) {
        state.supplied = surface->bufferSize();
        state.format = describeSuppliedFormat(surface);
        state.bufferKind = suppliedBufferKind(surface);
        state.scaling = m_renderedInputs.contains(window) && m_renderedInputs.value(window) == state.supplied;
    }

    // Colour is a property of the frame being painted. Outside a paint pass,
    // as when the settings page asks, it stays unknown rather than guessed.
    if (target) {
        state.targetTransform = int(target->transform().kind());
        const ColorDescription &colors = targetColors(*target);
        state.transferFunction = int(colors.transferFunction().type);
        state.referenceLuminance = colors.referenceLuminance();
    }
    return state;
}

void UpscaleEffect::describeApplication(UpscaleSnapshot &state, const Window *window,
                                        UpscalePresentation presentation) const
{
    // The cell a window presents in is its own, whoever answers for it.
    state.presentedAs = presentation;
    // Read identity fields separately; EffectWindow::windowClass combines them.
    const UpscaleApplication *known = upscaleApplicationForWindow(window);
    // A window no entry claims follows the global profile, which acts on it
    // once All applications is checked, and what that asked of it is reported
    // as for a listed program. Otherwise nothing was asked of it.
    if (!known && !upscaleResolveSettings(nullptr).acts()) {
        return;
    }
    if (known) {
        state.recognized = known->name;
    }
    state.method = upscaleMethodFor(known, state.presentedAs);
    // Keyed by the connection, not the program: another connection of the same
    // executable must not overwrite what the selected window was told.
    if (m_modeOverride && window->surface() && window->output()) {
        state.advertised = m_modeOverride->advertised(window->surface()->client(), window->output()->name());
        // Whether this program is told anything when it next starts: only the
        // entry its path selects before it has a window can say it then.
        const UpscaleBindAnswer answer = upscaleApplicationAtBind(upscaleProgramOf(window->surface()->client()));
        state.advertisableAtStart = answer.decided && answer.application == known;
    }
    // Auto is the resize on X11 and the surface scale on Wayland, and reports
    // what it actually asked for, the same as a method named outright. The
    // surface scale is also what an advertisement falls back to where it did
    // not reach the window, so any Wayland slot reports it.
    const bool x11 = upscaleIsX11(presentation);
    if (state.method == UpscaleMethod::X11Resize || (state.method == UpscaleMethod::Auto && x11)) {
        state.requested = m_x11Resolution->requested(window);
        state.requestFailure = m_x11Resolution->failure(window);
        state.x11Presentation = m_x11Resolution->presentation(window);
    } else if (!x11) {
        state.scaleRequested = m_waylandScale->requested(window);
        state.requested = m_waylandScale->requestedSize(window);
    }
}

static QString sizeFact(const QSize &size)
{
    return size.isValid() ? QStringLiteral("%1x%2").arg(size.width()).arg(size.height()) : QString();
}

QVariantMap UpscaleEffect::reportFacts(EffectWindow *window) const
{
    const UpscaleSnapshot state = snapshot(window, nullptr);
    const Window *internal = window->window();
    QVariantMap facts{
        // The path is the page's to reduce to what stays the same wherever the
        // game is installed; nothing else here says where anything is kept.
        {QStringLiteral("executable"), upscaleExecutableOf(internal)},
        {QStringLiteral("windowClass"), internal->resourceClass()},
        {QStringLiteral("instance"), internal->resourceName()},
        {QStringLiteral("x11"), upscaleIsX11(state.presentedAs)},
        // The key the entry states the method under, for this presentation.
        {QStringLiteral("presentation"), QString::fromLatin1(upscalePresentationKey(state.presentedAs))},
        {QStringLiteral("method"), upscaleMethodKey(state.method)},
        {QStringLiteral("advertised"), sizeFact(state.advertised)},
        {QStringLiteral("requested"), sizeFact(state.requested)},
        {QStringLiteral("requestFailure"), state.requestFailure},
        {QStringLiteral("scaleRequested"), state.scaleRequested},
        {QStringLiteral("supplied"), sizeFact(state.supplied)},
        {QStringLiteral("destination"), sizeFact(state.destination)},
        {QStringLiteral("outputScale"), state.outputScale},
        {QStringLiteral("build"), state.build},
        {QStringLiteral("kwin"), QString(KWIN_VERSION_STRING)},
        {QStringLiteral("graphics"), state.graphics},
    };
    if (const auto context = effects->openglContext()) {
        facts.insert(QStringLiteral("renderer"), QString::fromLatin1(context->renderer()));
        facts.insert(QStringLiteral("driver"), QString::fromLatin1(context->openglVersionString()));
    }
    return facts;
}

} // namespace KWin
