/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QRegularExpression>
#include <QTest>

// What the settings page makes a submitted entry's report of: the effect's
// facts about a window KWin's picker named, as it presents that window.
void UpscaleX11IntegrationTest::reportsWhatItObserved()
{
    configure(true);
    X11Client target(false);
    target.reportProcess();
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    const QRegularExpression active(QStringLiteral("^activeWindowId: (\\S+)$"), QRegularExpression::MultilineOption);
    const QString id = active.match(status()).captured(1);
    QVERIFY2(!id.isEmpty(), qPrintable(status()));

    QDBusInterface effect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    const QDBusReply<QVariantMap> reply = effect.call(QStringLiteral("reportFacts"), id);
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    const QVariantMap facts = reply.value();
    QCOMPARE(facts.value(QStringLiteral("windowClass")).toString(), QStringLiteral("upscale-x11-test"));
    QCOMPARE(facts.value(QStringLiteral("instance")).toString(), QStringLiteral("upscale-x11-test"));
    QVERIFY(facts.value(QStringLiteral("x11")).toBool());
    QCOMPARE(facts.value(QStringLiteral("presentation")).toString(), QStringLiteral("MethodX11FullScreen"));
    QCOMPARE(facts.value(QStringLiteral("method")).toString(), QStringLiteral("X11Resize"));
    QCOMPARE(facts.value(QStringLiteral("supplied")).toString(), QStringLiteral("1920x1080"));
    QCOMPARE(facts.value(QStringLiteral("destination")).toString(), QStringLiteral("3840x2160"));
    QCOMPARE(facts.value(QStringLiteral("output")).toString(), QStringLiteral("Virtual-0"));
    // The program is this test, which the window names as its own.
    QVERIFY2(facts.value(QStringLiteral("executable")).toString().endsWith(QLatin1String("/upscale_x11_integration_test")),
             qPrintable(facts.value(QStringLiteral("executable")).toString()));
    QVERIFY(!facts.value(QStringLiteral("kwin")).toString().isEmpty());
    // No renderer here: this session composites with QPainter and has no
    // OpenGL context to name one.
    // Whatever the page does with the rest, the window's title never leaves.
    QVERIFY(!facts.contains(QStringLiteral("caption")));

    // A window that is gone has nothing to report.
    const QDBusReply<QVariantMap> gone = effect.call(QStringLiteral("reportFacts"), QStringLiteral("{00000000-0000-0000-0000-000000000001}"));
    QVERIFY(gone.isValid());
    QVERIFY(gone.value().isEmpty());
}
