/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QObject>
#include <QPoint>

class UpscaleX11IntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void initialWindowedMapping_data();
    void initialWindowedMapping();
    void initialFullscreenMapping_data();
    void initialFullscreenMapping();
    void lifecycle_data();
    void lifecycle();
    void presentsWithoutEmulation();
    void keepsEmulatedPointerCoverage_data();
    void keepsEmulatedPointerCoverage();
    void expiresDepartedClientRefusal();
    void refusesUnavailableMode();
    void respectsPrimaryOutputRestriction();
    void retriesADroppedResizeOnce();
    void independentOutputRules();
    void matchesTheProgramBehindTheWindow();
    void autoResizesAnUnmeasuredX11Window();
    void repeatedFullscreenTransitions();
    void preservesModeOnFullscreenEntry_data();
    void preservesModeOnFullscreenEntry();

private:
    // The global resolution as kwinrc stores it, spelled out rather than taken
    // from the plugin: the stored number is the contract this test drives.
    enum class Stored {
        Quality = 2,
        Balanced = 3,
        Performance = 4,
    };
    QString status();
    void configure(bool enabled, Stored resolution = Stored::Performance);
    void movePointer(const QPoint &position);
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};
