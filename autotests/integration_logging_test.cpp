/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the effect writes to the log while a game runs: a line when something
// changed, not a line per frame. The test driver counts the effect's messages
// inside the compositor, where this process cannot read them.

#include "integration_test.h"
#include "wayland_client.h"

#include <QDBusReply>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTest>

static int effectMessages(const QString &status)
{
    static const QRegularExpression count(QStringLiteral("effectMessages: (\\d+)"));
    return count.match(status).captured(1).toInt();
}

// A game presenting frame after frame with nothing changing - the same buffer,
// the same window, the same settings - is logged once, when it began. Sixty
// frames later the effect has said nothing more.
void UpscaleIntegrationTest::logsTransitionsNotFrames()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedMode\nMinimumPixels=0\nOrder=1\n")));
    configureResolution(true, false, Stored::Quality);
    configureDisplay(false, false);
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(QSize(85, 85)));
    QTRY_VERIFY2(status().contains(QStringLiteral("Supplied input: 85 × 85")), qPrintable(status()));
    // Until every change of starting up has been logged: a few frames, and
    // the count no longer moving across them.
    QVERIFY(game.presentFrames(10));
    const int settled = effectMessages(status());
    QVERIFY(settled > 0);
    QVERIFY(game.presentFrames(60));
    QCOMPARE(effectMessages(status()), settled);
}
