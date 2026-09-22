/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "matching.h"

#include <KLocalizedString>

#include <vector>

namespace KWin
{

bool UpscaleGates::statesExecutable() const
{
    return executable.isStated();
}

bool UpscaleGates::statesWindow() const
{
    return windowClass.isStated() || instance.isStated();
}

bool UpscaleGates::matches(const UpscaleIdentity &identity) const
{
    if (!statesExecutable() && !statesWindow()) {
        return false;
    }
    if (statesExecutable() && !executable.matches(identity.executable)) {
        return false;
    }
    if (windowClass.isStated() && !windowClass.matches(identity.windowClass)) {
        return false;
    }
    return !instance.isStated() || instance.matches(identity.instance);
}

UpscaleGates upscaleGatesOf(const UpscaleApplication &application)
{
    return {
        UpscalePattern(application.executable, application.executableMatch),
        UpscalePattern(application.windowClass, application.windowClassMatch),
        UpscalePattern(application.instance, application.instanceMatch),
    };
}

QString upscaleIdentityProblem(const UpscaleApplication &application)
{
    const UpscaleGates gates = upscaleGatesOf(application);
    if (!gates.statesExecutable() && !gates.statesWindow()) {
        return i18n("It states neither a program nor a window class or instance, so it would match every window.");
    }
    for (const UpscalePattern *pattern : {&gates.executable, &gates.windowClass, &gates.instance}) {
        if (const QString problem = pattern->problem(); !problem.isEmpty()) {
            return problem;
        }
    }
    return QString();
}

QString upscaleAdvertisementProblem(const UpscaleApplication &application)
{
    const UpscaleGates gates = upscaleGatesOf(application);
    if (!upscaleIsAdvertisement(application.methods[std::size_t(upscaleAdvertisedPresentation())])
        || (gates.statesExecutable() && !gates.statesWindow())) {
        return QString();
    }
    return i18n("Its Wayland fullscreen method is sent before the window exists, so it needs a program and no "
                "window class or instance.");
}

// The stored list's patterns, compiled once per reading of the list rather
// than on every match. The generation says when the list was read again.
static const std::vector<UpscaleGates> &compiledGates()
{
    static std::vector<UpscaleGates> s_gates;
    static quint64 s_generation = 0;
    // Read the list first: the first reading is what gives it a generation.
    const std::vector<UpscaleApplication> &applications = upscaleApplications();
    if (s_generation != upscaleApplicationsGeneration()) {
        s_gates.clear();
        for (const UpscaleApplication &application : applications) {
            s_gates.push_back(upscaleGatesOf(application));
        }
        s_generation = upscaleApplicationsGeneration();
    }
    return s_gates;
}

// A profile that is switched off takes no part in matching, so a later one can
// claim the window and, failing that, the global profile does. That is how a
// user shadows an entry this package ships: their own entry takes a lower
// Order, or they switch the shipped one off.
//
// It reads as leaving the game alone because the global profile is switched
// off by default, so falling all the way through means nothing acts on it. A
// user who has deliberately switched the global profile on has asked for
// unlisted applications to be handled, and this game is then one of them.
const UpscaleApplication *upscaleApplicationFor(const UpscaleIdentity &identity)
{
    const std::vector<UpscaleApplication> &applications = upscaleApplications();
    const std::vector<UpscaleGates> &gates = compiledGates();
    for (std::size_t index = 0; index < applications.size(); ++index) {
        if (applications[index].enabled && gates[index].matches(identity)) {
            return &applications[index];
        }
    }
    return nullptr;
}

UpscaleBindAnswer upscaleApplicationAtBind(const QString &executable)
{
    if (executable.isEmpty()) {
        return {};
    }
    const std::vector<UpscaleApplication> &applications = upscaleApplications();
    const std::vector<UpscaleGates> &gates = compiledGates();
    for (std::size_t index = 0; index < applications.size(); ++index) {
        const UpscaleGates &gate = gates[index];
        if (!applications[index].enabled || !gate.statesExecutable() || !gate.executable.matches(executable)) {
            continue;
        }
        if (gate.statesWindow()) {
            return {nullptr, false};
        }
        return {&applications[index], true};
    }
    // Nothing in the list describes this program. A null profile is how the
    // caller asks for the global one, which answers for every program no
    // profile claimed.
    return {};
}

} // namespace KWin
