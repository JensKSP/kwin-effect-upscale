/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A game that leaves fullscreen and asks for it again before it has read what
// leaving it caused, as a player pressing Alt+Enter twice does, and as a slow
// machine makes of any pair of transitions: the nightly's KWin 6.6 runner
// failed repeatedFullscreenTransitions on 2026-09-28 this way.

#include "x11_client.h"
#include "x11_integration_test.h"

#include <QTest>

// Leaving fullscreen gives the request back, and the client answers by
// withdrawing the emulated mode it held. The request for the new fullscreen
// has to wait for that answer: KWin 6.6 sizes the window from the mode's
// property whenever it changes, so a request made before the withdrawal
// reaches KWin is undone by it, and validation fails the request. The effect
// retries once per program, so the second such pair would leave the game
// unscaled for good. Both messages here leave before the client has read
// anything in between.
void UpscaleX11IntegrationTest::reenteringFullscreenAtOnce()
{
    X11Client target;
    const QSize reduced(1920, 1080);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(QPoint(0, 0), reduced), false));
    QTRY_COMPARE_WITH_TIMEOUT(target.geometry().size(), reduced, 30000);
    QVERIFY(target.mode(reduced));
    configure(true);
    target.fullscreen(true);
    const QString scaled = QStringLiteral("QSize(1920, 1080) QSizeF(3840, 2160)");
    QTRY_VERIFY2_WITH_TIMEOUT(status().contains(scaled), qPrintable(status()), 30000);
    for (int pair = 0; pair < 4; ++pair) {
        const qsizetype before = target.configuredSizes().size();
        target.fullscreen(false);
        target.fullscreen(true);
        // The restore's configure has reached the client once KWin has read
        // the first message; the second follows it in the same queue.
        QTRY_VERIFY2_WITH_TIMEOUT(target.configuredSizes().sliced(before).contains(QSize(3840, 2160)),
                                  qPrintable(QDebug::toString(target.configuredSizes().sliced(before))), 30000);
        UPSCALE_TRY_SETTLED();
        // Validation judges a request three seconds after it was made, and
        // until then the status cannot say whether it held.
        QTest::qWait(3500);
        QVERIFY2(status().contains(scaled), qPrintable(status()));
        QVERIFY2(!status().contains(QStringLiteral("request failed")), qPrintable(status()));
    }
}
