/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "display_test.h"

#include "application.h"
#include "upscaleconfig.h"

void UpscaleDisplayTest::perGameDisplaySettings()
{
    UpscaleConfig::setOsd(true);
    UpscaleConfig::setOsdDetection(false);
    UpscaleConfig::setOsdSummary(false);
    UpscaleConfig::setOsdStatistics(false);
    UpscaleConfig::setOsdDeveloper(false);
    UpscaleDisplay display;
    display.reconfigure();
    UpscaleSnapshot state;
    state.window = QStringLiteral("Extreme Tux Racer");
    state.supplied = QSize(2560, 1440);
    state.destination = QSize(3840, 2160);

    // A game can keep composition active and draw its counter even when
    // every global display block is disabled.
    UpscaleApplication game;
    game.overrides[std::size_t(UpscaleSetting::OsdStatistics)] = 1;
    UpscaleSettings settings = upscaleResolveSettings(&game);
    QVERIFY(!display.enabled());
    QVERIFY(display.enabled(settings));
    QVERIFY(display.activeFor(nullptr, settings));
    display.update(state, nullptr, settings);
    QVERIFY(display.text().contains(QStringLiteral("FPS")));
    QVERIFY(!paint(display).isNull());
    QVERIFY(display.drawn());

    // The opposite override must suppress a globally enabled counter too.
    UpscaleConfig::setOsdStatistics(true);
    game.overrides[std::size_t(UpscaleSetting::OsdStatistics)] = 0;
    settings = upscaleResolveSettings(&game);
    QVERIFY(display.enabled());
    QVERIFY(!display.enabled(settings));
    QVERIFY(!display.activeFor(nullptr, settings));
    display.update(state, nullptr, settings);
    QVERIFY(display.text().isEmpty());
    QVERIFY(!paint(display).isNull());
    QVERIFY(!display.drawn());

    // Selecting a profile without the override goes back to inheritance,
    // without reloading the global configuration or recreating the display.
    game.overrides[std::size_t(UpscaleSetting::OsdStatistics)].reset();
    settings = upscaleResolveSettings(&game);
    QVERIFY(display.activeFor(nullptr, settings));
    display.update(state, nullptr, settings);
    QVERIFY(display.text().contains(QStringLiteral("FPS")));
    QVERIFY(!paint(display).isNull());
    QVERIFY(display.drawn());
}
