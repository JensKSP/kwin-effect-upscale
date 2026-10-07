/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "game_entry.h"

#include "effect/effect.h"
#include "effect/effecthandler.h"
#include "main.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDebug>
#include <QScopedValueRollback>

namespace KWin
{

// Scoped only around explicit upstream cleanup unloads, never test bodies or
// client teardown. Unexpected destruction keeps invalidating the comparison.
inline bool s_upscaleConformanceFixtureUnloading = false;

// Included only in a private copy of KDE's test application. The original
// tests still own their clients and assertions. All comparison arms use the
// same OpenGL scene; setting an outer desktop's compositor changes nothing
// about the compositor each upstream executable creates here.
inline void configureUpscaleConformance()
{
    if (!qEnvironmentVariableIsSet("UPSCALE_CONFORMANCE_ARM")) {
        return;
    }
    const QString arm = qEnvironmentVariable("UPSCALE_CONFORMANCE_ARM");
    if (arm != QLatin1String("absent") && arm != QLatin1String("idle") && arm != QLatin1String("active")) {
        qFatal("Unknown conformance arm");
    }
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    // Loading is explicit below, after the compositor has a current context.
    KConfigGroup(kwinApp()->config(), QStringLiteral("Plugins")).writeEntry("upscaleEnabled", false);
    const auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.deleteGroup();
    group.writeEntry("UnlistedApplications", arm == QLatin1String("active"));
    // All games acts only for a game, so the active arm declares the upstream
    // test's own program, whose clients these are, one.
    if (!upscaleDeclareGame(QCoreApplication::applicationFilePath(), arm == QLatin1String("active"))) {
        qFatal("Conformance could not declare its program a game");
    }
    group.writeEntry("Resolution", 4);
    group.writeEntry("MinimumPixels", 0);
    group.writeEntry("Sharpening", false);
    group.writeEntry("Osd", false);
    group.writeEntry("X11Proxy", false);
    group.sync();
}

inline void loadUpscaleConformance()
{
    if (!qEnvironmentVariableIsSet("UPSCALE_CONFORMANCE_ARM")) {
        return;
    }
    const QString arm = qEnvironmentVariable("UPSCALE_CONFORMANCE_ARM");
    if (!effects || !effects->isOpenGLCompositing()) {
        qFatal("Conformance requires the real OpenGL compositor in every arm");
    }
    const bool wasLoaded = effects->isEffectLoaded(QStringLiteral("upscale"));
    if (arm != QLatin1String("absent") && !wasLoaded && !effects->loadEffect(QStringLiteral("upscale"))) {
        qFatal("Conformance could not load the production upscale effect");
    }
    const bool loaded = effects->isEffectLoaded(QStringLiteral("upscale"));
    if (loaded != (arm != QLatin1String("absent"))) {
        qFatal("Conformance loaded the wrong effect state");
    }
    qInfo().noquote() << "UPSCALE_CONFORMANCE arm=" + arm
                      << "opengl=1 loaded=" + QString::number(loaded);
    // A case that unloads the effect runs the rest of itself without it, and
    // has to say so. KWin 6.3.6 announces an unload with no public signal,
    // but the unloaded effect is destroyed, and that is heard the same way on
    // every version.
    if (loaded && !wasLoaded) {
        Effect *effect = effects->findEffect(QStringLiteral("upscale"));
        QObject::connect(effect, &QObject::destroyed, []() {
            qInfo().noquote() << (s_upscaleConformanceFixtureUnloading
                                      ? "UPSCALE_CONFORMANCE cleanup loaded=0"
                                      : "UPSCALE_CONFORMANCE state loaded=0");
        });
    }
    // Do not poll status here: candidate queries can themselves initiate
    // requests. The upstream comparison must exercise normal compositor
    // activity. The explicit scaling cases inspect status only after their
    // client has received and answered a request through that normal path.
}

} // namespace KWin
