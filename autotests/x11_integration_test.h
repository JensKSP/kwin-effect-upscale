/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QObject>
#include <QPoint>

// Waits until the effect has nothing in flight for any X11 window: no restore,
// no request waiting to be sent, no client still to withdraw its mode. A
// geometry that already matches before a reconfiguration says nothing about
// what the reconfiguration did, so a case judges it only after this. Bounded
// by the withdrawal fallback plus the nightly's slowest runner.
#define UPSCALE_TRY_SETTLED() \
    QTRY_VERIFY2_WITH_TIMEOUT(status().contains(QStringLiteral("x11Settled: true")), qPrintable(status()), 30000)

// Waits until the effect's validation has judged @p count more requests than
// the @p before a case noted before acting: what a request left behind is
// judged only then, three seconds after it was made, however slowly the
// machine got there. Bounded like the waits above.
#define UPSCALE_TRY_JUDGED(before, count) \
    QTRY_VERIFY2_WITH_TIMEOUT(judgements() >= (before) + (count), qPrintable(status()), 30000)

class UpscaleX11IntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void initialWindowedMapping_data();
    void initialWindowedMapping();
    void withdrawnWhileHeld();
    void initialFullscreenMapping_data();
    void initialFullscreenMapping();
    void lifecycle_data();
    void lifecycle();
    void presentsWithoutEmulation();
    void refreshesStartupInputShape();
    void coversPointerWithoutEmulatedMode();
    void aConfinedPointerReachesTheWholeWindow();
    void keepsTheKeyboardWhereThePointerIs();
    void letsAPresentedGameLockThePointer();
    void coversTheScreenItWasGiven();
    void winePrefixEligibility_data();
    void winePrefixEligibility();
    void leavesWineToTheProxy_data();
    void leavesWineToTheProxy();
    void reportsWhatItObserved();
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
    void aGameThatCrashesWhileResizedIsLetGo();
    void anUnnamedProgramIsHeldAtItsFirstMapping();
    void reenteringFullscreenAtOnce();
    void fitsAnEmulatedModeBetweenBars();
    void centresAPresentedWindowByAWholeFactor();

private:
    // The global resolution as kwinrc stores it, spelled out rather than taken
    // from the plugin: the stored number is the contract this test drives.
    enum class Stored {
        Quality = 2,
        Balanced = 3,
        Performance = 4,
    };
    QString status();
    /** How often the effect's X11 validation has judged a request so far. */
    int judgements();
    void configure(bool enabled, Stored resolution = Stored::Performance);
    void movePointer(const QPoint &position);
    /** @p device, which is where an X11 window is, in KWin's logical pixels. */
    static QPoint logical(const QPoint &device);
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};
