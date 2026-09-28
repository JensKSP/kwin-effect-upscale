/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effecthandler.h"
#include "main.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QDebug>

namespace KWin
{

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
    if (arm != QLatin1String("absent") && !effects->loadEffect(QStringLiteral("upscale"))) {
        qFatal("Conformance could not load the production upscale effect");
    }
    const bool loaded = effects->isEffectLoaded(QStringLiteral("upscale"));
    if (loaded != (arm != QLatin1String("absent"))) {
        qFatal("Conformance loaded the wrong effect state");
    }
    qInfo().noquote() << "UPSCALE_CONFORMANCE arm=" + arm
                      << "opengl=1 loaded=" + QString::number(loaded);
    QObject::connect(effects, &EffectsHandler::effectsChanged, effects, []() {
        qInfo().noquote() << "UPSCALE_CONFORMANCE state loaded="
                + QString::number(effects->isEffectLoaded(QStringLiteral("upscale")));
    });
    // Do not poll status here: candidate queries can themselves initiate
    // requests. The upstream comparison must exercise normal compositor
    // activity. The explicit scaling cases inspect status only after their
    // client has received and answered a request through that normal path.
}

} // namespace KWin
