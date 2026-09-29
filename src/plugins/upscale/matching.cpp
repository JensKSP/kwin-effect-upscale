/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "matching.h"

#include <KLocalizedString>
#include <QHash>

#include <algorithm>
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
    if (!upscaleIsAdvertisement(upscaleMethodFor(&application, upscaleAdvertisedPresentation()))
        || (gates.statesExecutable() && !gates.statesWindow())) {
        return QString();
    }
    return i18n("Its Wayland fullscreen method is sent before the window exists, so it needs a program and no "
                "window class or instance.");
}

namespace
{

// The stored list, compiled once per reading of it rather than on every match,
// and indexed, as decided on 2026-09-29 so that the list can grow by
// submissions. An entry is filed under one field that has the same last
// component in every value it matches - its program's file name first, then its
// window class, then its instance - because a window whose value differs there
// fails that gate. What no field pins is tried for every window.
struct CompiledList
{
    std::vector<UpscaleGates> gates;
    QHash<QString, std::vector<std::size_t>> byProgram;
    QHash<QString, std::vector<std::size_t>> byClass;
    QHash<QString, std::vector<std::size_t>> byInstance;
    std::vector<std::size_t> unfiled;
    // The entries stating a program that pins no file name, which the answer
    // at bind has to try as well.
    std::vector<std::size_t> unfiledPrograms;
};

QString lastComponent(const QString &value)
{
    return value.section(QLatin1Char('/'), -1);
}

void fileEntry(CompiledList &list, std::size_t index)
{
    const UpscaleGates &gates = list.gates[index];
    if (const QString program = gates.executable.fixedLastComponent(); !program.isEmpty()) {
        list.byProgram[program].push_back(index);
        return;
    }
    if (gates.statesExecutable()) {
        list.unfiledPrograms.push_back(index);
    }
    if (const QString windowClass = gates.windowClass.fixedLastComponent(); !windowClass.isEmpty()) {
        list.byClass[windowClass].push_back(index);
    } else if (const QString instance = gates.instance.fixedLastComponent(); !instance.isEmpty()) {
        list.byInstance[instance].push_back(index);
    } else {
        list.unfiled.push_back(index);
    }
}

const CompiledList &compiledList()
{
    static CompiledList s_list;
    static quint64 s_generation = 0;
    // Read the list first: the first reading is what gives it a generation.
    const std::vector<UpscaleApplication> &applications = upscaleApplications();
    if (s_generation != upscaleApplicationsGeneration()) {
        s_list = CompiledList();
        for (std::size_t index = 0; index < applications.size(); ++index) {
            s_list.gates.push_back(upscaleGatesOf(applications[index]));
            fileEntry(s_list, index);
        }
        s_generation = upscaleApplicationsGeneration();
    }
    return s_list;
}

// The entries filed under these keys and those always tried, in list order,
// which is the order they are matched in.
std::vector<std::size_t> candidates(const QHash<QString, std::vector<std::size_t>> &index, const QString &key,
                                    std::vector<std::size_t> found)
{
    if (const auto filed = index.constFind(key); !key.isEmpty() && filed != index.cend()) {
        found.insert(found.end(), filed->begin(), filed->end());
    }
    return found;
}

} // namespace

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
    const CompiledList &list = compiledList();
    std::vector<std::size_t> found = candidates(list.byProgram, lastComponent(identity.executable), list.unfiled);
    found = candidates(list.byClass, lastComponent(identity.windowClass), std::move(found));
    found = candidates(list.byInstance, lastComponent(identity.instance), std::move(found));
    std::ranges::sort(found);
    for (const std::size_t index : found) {
        if (applications[index].enabled && list.gates[index].matches(identity)) {
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
    const CompiledList &list = compiledList();
    std::vector<std::size_t> found = candidates(list.byProgram, lastComponent(executable), list.unfiledPrograms);
    std::ranges::sort(found);
    for (const std::size_t index : found) {
        const UpscaleGates &gate = list.gates[index];
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
