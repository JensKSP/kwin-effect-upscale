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
#include "modeoverride.h"
#include "resolution.h"
#include "snapshot.h"
#include "upscaleconfig.h"
#include "x11resolution.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "wayland/surface.h"
#include "window.h"

#include <KLocalizedString>

namespace KWin
{

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
    describeApplication(state, window->window());
    state.activeWindow = effects->activeWindow() == window;
    state.fullScreen = window->isFullScreen();
    state.blocksScanout = blocksDirectScanout();

    state.enabled = m_enabled;
    state.failed = m_failed;
    state.maximumTexture = m_maximumTexture;
    state.preset = static_cast<ResolutionPreset>(UpscaleConfig::preset());
    state.percentage = UpscaleConfig::percentage();
    state.sharpening = m_strength;

    if (UpscaleOutput *output = window->screen()) {
        state.output = output->name();
        state.destination = output->pixelSize();
        state.outputScale = output->scale();
        state.outputArea = output->geometryF();
        state.desired = desiredResolution({state.destination.width(), state.destination.height()}, state.preset, state.percentage);
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

void UpscaleEffect::describeApplication(UpscaleSnapshot &state, const Window *window) const
{
    // Read identity fields separately; EffectWindow::windowClass combines them.
    const UpscaleApplication *known = window
        ? upscaleApplicationForIdentity(window->resourceClass(), window->resourceName())
        : nullptr;
    if (!known) {
        return;
    }
    state.recognized = known->name;
    state.method = known->method;
    // Keyed by the connection, not the program: another connection of the same
    // executable must not overwrite what the selected window was told.
    if (m_modeOverride && window->surface() && window->output()) {
        state.advertised = m_modeOverride->advertised(window->surface()->client(), window->output()->name());
    }
    if (known->method == UpscaleControlMethod::X11Resize) {
        state.requested = m_x11Resolution->requested(window);
        state.requestFailure = m_x11Resolution->failure(window);
        state.x11Presentation = m_x11Resolution->presentation(window);
    }
}

} // namespace KWin
