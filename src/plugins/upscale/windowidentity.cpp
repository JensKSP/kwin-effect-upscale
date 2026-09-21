/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "windowidentity.h"

#include "matching.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "utils/executable_path.h"
#include "window.h"

#include <QDBusConnection>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QUuid>

// Its own category rather than the effect's, which upscale.cpp defines: this
// file is also built into tests that do not build the effect around it.
Q_LOGGING_CATEGORY(KWIN_UPSCALE_MATCHING, "kwin_effect_upscale.matching", QtWarningMsg)

namespace KWin
{

QString upscaleExecutableOf(const Window *window)
{
    const pid_t pid = window ? window->pid() : 0;
    return pid > 0 ? executablePathFromPid(pid) : QString();
}

namespace
{

struct Resolved
{
    // Which window this is about. The hash below is keyed by address, and a
    // window created where a closed one was must not inherit its path.
    QPointer<const Window> window;
    quint64 generation = 0;
    QString windowClass;
    QString instance;
    QString executable;
    const UpscaleApplication *application = nullptr;
};

}

static QHash<const Window *, Resolved> resolvedWindows;

// An entry that can never match says so once per reading of the list, in the
// journal of the session it is in. The editor refuses to store one; this is
// for a file written by hand.
static void reportProblems(quint64 generation)
{
    static quint64 s_reported = 0;
    if (s_reported == generation) {
        return;
    }
    s_reported = generation;
    for (const UpscaleApplication &application : upscaleApplications()) {
        if (const QString problem = upscaleIdentityProblem(application); !problem.isEmpty()) {
            qCWarning(KWIN_UPSCALE_MATCHING) << "Application" << application.id << "never matches:" << problem;
        }
    }
}

const UpscaleApplication *upscaleApplicationForWindow(const Window *window)
{
    if (!window) {
        return nullptr;
    }
    upscaleApplications();
    const quint64 generation = upscaleApplicationsGeneration();
    reportProblems(generation);
    auto found = resolvedWindows.find(window);
    if (found == resolvedWindows.end() || found->window != window) {
        // Forget the windows that are gone before remembering another, so the
        // hash holds what is open rather than everything that ever was.
        resolvedWindows.removeIf([](const QHash<const Window *, Resolved>::iterator &entry) {
            return entry->window.isNull();
        });
        Resolved fresh;
        fresh.window = window;
        fresh.executable = upscaleExecutableOf(window);
        found = resolvedWindows.insert(window, fresh);
    }
    Resolved &resolved = *found;
    if (resolved.generation != generation || resolved.windowClass != window->resourceClass()
        || resolved.instance != window->resourceName()) {
        resolved.generation = generation;
        resolved.windowClass = window->resourceClass();
        resolved.instance = window->resourceName();
        resolved.application = upscaleApplicationFor({resolved.executable, resolved.windowClass, resolved.instance});
    }
    return resolved.application;
}

UpscaleIdentityService::UpscaleIdentityService(QObject *parent)
    : QObject(parent)
    , m_handler(effects)
{
    // Unregistered by QtDBus when this object goes, with the effect.
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                 QStringLiteral("org.kde.KWin.Effect.Upscale1"), this,
                                                 QDBusConnection::ExportAllSlots);
}

QStringList UpscaleIdentityService::windowsMatching(const QVariantMap &entry) const
{
    const auto field = [&entry](const char *key) {
        return entry.value(QLatin1String(key)).toString();
    };
    const UpscaleGates gates{
        UpscalePattern(field("Executable"), upscaleStringMatchFromKey(field("ExecutableMatch"))),
        UpscalePattern(field("WindowClass"), upscaleStringMatchFromKey(field("WindowClassMatch"))),
        UpscalePattern(field("Instance"), upscaleStringMatchFromKey(field("InstanceMatch"))),
    };
    QStringList captions;
    if (!m_handler) {
        return captions;
    }
    const QList<EffectWindow *> windows = m_handler->stackingOrder();
    for (const EffectWindow *window : windows) {
        const Window *internal = window->window();
        if (!internal || internal->isDeleted() || !window->isNormalWindow()) {
            continue;
        }
        if (gates.matches({upscaleExecutableOf(internal), internal->resourceClass(), internal->resourceName()})) {
            captions.append(window->caption());
        }
    }
    return captions;
}

QString UpscaleIdentityService::executablePath(const QString &window) const
{
    const EffectWindow *found = m_handler ? m_handler->findWindow(QUuid::fromString(window)) : nullptr;
    return found ? upscaleExecutableOf(found->window()) : QString();
}

} // namespace KWin
