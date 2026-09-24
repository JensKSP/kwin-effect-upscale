/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "diagnostics.h"

#include "application.h"
#include "windowidentity.h"

#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "wayland/pointerconstraints_v1.h"
#include "wayland/surface.h"
#include "window.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

template<typename Surface>
static auto pointerRegion(Surface *surface, bool locked)
{
    // Newer KWin stores committed constraint regions on the surface. Earlier
    // releases expose them on the constraint itself. Read the committed value
    // through whichever interface the running KWin provides.
    if constexpr (requires { surface->lockedPointerRegion(); }) {
        using Region = decltype(surface->input());
        return locked ? surface->lockedPointerRegion().value_or(Region())
                      : surface->confinedPointerRegion().value_or(Region());
    } else {
        return locked ? surface->lockedPointer()->region() : surface->confinedPointer()->region();
    }
}

UpscaleDiagnostics::State UpscaleDiagnostics::readState(EffectWindow *window, const UpscaleSettings &settings)
{
    Window *internal = window->window();
    const UpscaleApplication *application = upscaleApplicationForWindow(internal);
    State observed;
    observed.profile = application ? application->id : QStringLiteral("global");
    observed.method = upscaleMethodFor(application, upscalePresentationOf(window));
    observed.enabled = settings.acts();
    for (const UpscaleSettingInfo &entry : upscaleSettingTable()) {
        observed.settings[std::size_t(entry.setting)] = settings.value(entry.setting);
    }
    SurfaceItem *surface = window->windowItem() ? window->windowItem()->surfaceItem() : nullptr;
    observed.buffer = surface ? surface->bufferSize() : QSize();
    observed.surface = internal->surface() ? internal->surface()->size() : QSizeF();
    observed.destination = surface ? surface->destinationSize() : QSizeF();
    if (SurfaceInterface *inputSurface = internal->surface()) {
        observed.input = inputSurface->input();
        if (LockedPointerV1Interface *lock = inputSurface->lockedPointer()) {
            observed.pointerLock = int(lock->isLocked());
            observed.constraint = pointerRegion(inputSurface, true);
        }
        if (ConfinedPointerV1Interface *confined = inputSurface->confinedPointer()) {
            observed.pointerConfinement = int(confined->isConfined());
            observed.constraint = pointerRegion(inputSurface, false);
        }
    }
    observed.frame = window->frameGeometry();
    observed.output = window->screen() ? window->screen()->name() : QString();
    observed.outputSize = window->screen() ? window->screen()->pixelSize() : QSize();
    return observed;
}

void UpscaleDiagnostics::logSettings(EffectWindow *window, const QString &id, const State &observed)
{
    QStringList values;
    for (const UpscaleSettingInfo &entry : upscaleSettingTable()) {
        const int value = observed.settings[std::size_t(entry.setting)];
        values.append(QString::fromLatin1(entry.key) + QLatin1Char('=') + (entry.name ? entry.name(value) : QString::number(value)));
    }
    qCInfo(KWIN_UPSCALE) << "Effective settings: window" << id << "pid" << window->pid()
                         << "application" << window->windowClass() << "profile" << observed.profile
                         << "protocol" << (window->isX11Client() ? "X11" : "Wayland")
                         << "enabled" << observed.enabled << "method" << upscaleMethodKey(observed.method)
                         << values.join(QLatin1Char(' '));
}

void UpscaleDiagnostics::observe(EffectWindow *window, const UpscaleSettings &settings, bool selected, UpscaleRefusal refusal)
{
    Window *internal = window->window();
    if (!internal) {
        return;
    }
    State observed = readState(window, settings);
    observed.selected = selected;
    observed.refusal = refusal;
    const auto previous = m_states.constFind(window);
    if (previous != m_states.cend() && *previous == observed) {
        return;
    }
    // Comparing small values above is the steady-state cost. Text is composed
    // only on a transition, with no protocol round trip or GPU query.
    const bool first = previous == m_states.cend();
    const QString id = internal->internalId().toString(QUuid::WithoutBraces);
    if (first || previous->profile != observed.profile || previous->method != observed.method
        || previous->enabled != observed.enabled || previous->settings != observed.settings) {
        logSettings(window, id, observed);
    }
    qCInfo(KWIN_UPSCALE) << "Observed: window" << id << "buffer" << observed.buffer
                         << "surface" << observed.surface << "presentation" << observed.destination
                         << "frame" << observed.frame << "output" << observed.output << observed.outputSize
                         << "selected" << selected << "refusal" << describeRefusal(refusal);
    if (first || previous->input != observed.input || previous->constraint != observed.constraint
        || previous->pointerLock != observed.pointerLock || previous->pointerConfinement != observed.pointerConfinement) {
        // -1 means no constraint, 0 requested, 1 engaged. These are what the
        // compositor actually holds, not an inference from the game's cursor.
        qCInfo(KWIN_UPSCALE) << "Input observed: window" << id << "region" << observed.input
                             << "lock" << observed.pointerLock << "confinement" << observed.pointerConfinement
                             << "constraint region" << observed.constraint;
    }
    if (first) {
        connect(window, &QObject::destroyed, this, [this, window, id]() {
            qCInfo(KWIN_UPSCALE) << "Window removed:" << id;
            m_states.remove(window);
        });
    }
    m_states.insert(window, observed);
}

} // namespace KWin
