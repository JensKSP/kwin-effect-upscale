/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTest>

void UpscaleX11IntegrationTest::initialFullscreenMapping_data()
{
    QTest::addColumn<int>("output");
    QTest::newRow("primary") << 0;
    QTest::newRow("secondary") << 1;
}

void UpscaleX11IntegrationTest::initialFullscreenMapping()
{
    QFETCH(int, output);
    configure(true);
    X11Client target(false);
    const QPoint position(output * 3840, 0);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(position, QSize(1024, 768)), false));
    target.fullscreen(true);
    QVERIFY(target.waitForMapping());
    QCOMPARE(target.geometry(), QRect(position, QSize(1920, 1080)));
}

void UpscaleX11IntegrationTest::initialWindowedMapping_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("windowed-timeout") << 0;
    QTest::newRow("effect-unloaded") << 1;
    QTest::newRow("effect-disabled") << 2;
    QTest::newRow("unrelated-window") << 3;
}

void UpscaleX11IntegrationTest::initialWindowedMapping()
{
    QFETCH(int, action);
    configure(true);
    X11Client target(false);
    const QByteArray identity = action == 3 ? QByteArrayLiteral("unrelated-x11-test") : QByteArrayLiteral("upscale-x11-test");
    const QRect initial(0, 0, 1024, 768);
    QVERIFY(target.show(identity, initial, false));
    if (action == 1) {
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    } else if (action == 2) {
        configure(false);
    }
    QVERIFY(target.waitForMapping());
    QCOMPARE(target.geometry().size(), initial.size());
    QVERIFY(!target.isFullscreen());
}

// A window its client withdraws while its first mapping is held stays
// withdrawn, and the client hears that it was unmapped: KWin sees the mapping
// and the withdrawal in the order the client sent them. A toolkit popup shown
// and hidden at once does this; replayed after the hold, the mapping showed a
// window nobody wanted any more.
void UpscaleX11IntegrationTest::withdrawnWhileHeld()
{
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1024, 768), false));
    target.withdraw();
    QTRY_VERIFY(target.unmapNotifies() > 0);
    // Past the hold, after which a mapping still pending would be released.
    QTest::qWait(300);
    QVERIFY(!target.isViewable());
}

// A game fills the screen it believes in. Once its connection has been
// answered with a smaller one, its borderless window is that size and no
// larger, and nothing about such a window says fullscreen. Measured against
// the output it would cover only part of one; measured against the screen it
// was given it covers all of it, which is what the effect acts on. This is the
// shape a Wine or Proton game in borderless mode arrives in.
void UpscaleX11IntegrationTest::coversTheScreenItWasGiven()
{
    configure(true);
    // Answer this process the way the transport does. The reply names the
    // screen the program is to believe in, so the window is made that size
    // rather than a size this test would otherwise have to predict.
    QDBusInterface policy(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    const QDBusReply<QVariantMap> answer =
        policy.call(QStringLiteral("x11ConnectionPolicy"), uint(QCoreApplication::applicationPid()),
                    QStringList{QStringLiteral("upscale-x11-test")});
    QVERIFY2(answer.isValid(), qPrintable(answer.error().message()));
    const QString reason = answer.value().value(QStringLiteral("reason")).toString();
    // A connection is answered before any window exists, so what it names is a
    // screen and not an arrangement of them. The session that runs two outputs
    // cannot produce one; the single-screen session beside it does, and that is
    // where this case is measured.
    if (reason.contains(QStringLiteral("one enabled output"))) {
        QSKIP("a connection is answered only for a single screen");
    }
    const QSize given(answer.value().value(QStringLiteral("width")).toInt(),
                      answer.value().value(QStringLiteral("height")).toInt());
    QVERIFY2(!given.isEmpty(), qPrintable(reason));

    X11Client target(false);
    target.reportProcess();
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(QPoint(0, 0), given), false));
    QVERIFY(target.waitForMapping());
    QVERIFY(!target.isFullscreen());
    QTRY_VERIFY2(!status().contains(QStringLiteral("not fullscreen or a selected borderless window")),
                 qPrintable(status()));
}
