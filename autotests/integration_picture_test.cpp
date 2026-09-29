/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Where the pointer lands on a Wayland game whose picture has bars beside it:
// on the picture, as the game maps its surface onto its buffer, and never in a
// bar while the game holds it confined.

#include "integration_test.h"
#include "wayland_client.h"

#include <QDBusReply>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSocketNotifier>
#include <QTest>

#include <cmath>

// Read and removed by the test driver inside the compositor, which moves its
// pointer device there; see the X11 test's movePointer().
static void movePointer(const QPoint &position)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QStringLiteral("/upscale-test-pointer");
    QSaveFile request(path);
    QVERIFY(request.open(QIODevice::WriteOnly));
    QVERIFY(request.write(QByteArray::number(position.x()) + ' ' + QByteArray::number(position.y())) > 0);
    QVERIFY(request.commit());
    QTRY_VERIFY(!QFile::exists(path));
}

// The same place within the 1/256 a Wayland coordinate is counted in.
static bool near(const QPointF &arrived, const QPointF &expected)
{
    return std::abs(arrived.x() - expected.x()) < 0.01 && std::abs(arrived.y() - expected.y()) < 0.01;
}

// 64 × 80 on the session's 128 × 128 screen is enlarged 1.6 times to
// 102 × 128, 13 pixels in from the left, and the game's surface still covers
// the screen. A point on the picture reaches the surface where the game drew
// what is under it: its offset into the picture, scaled up by 128 / 102.
void UpscaleIntegrationTest::mapsThePointerOntoThePicture()
{
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
    const auto unload = qScopeGuard([this]() {
        writeCatalogue(QString());
        m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    });
    // Its own limit, because an earlier case in the session raises the global
    // one to this screen's size.
    writeCatalogue(integrationEntry(QStringLiteral("MinimumPixels=0\n")));
    configure(false, false);
    WaylandClient game;
    QVERIFY(game.initialize());
    QSocketNotifier notifier(game.descriptor(), QSocketNotifier::Read);
    connect(&notifier, &QSocketNotifier::activated, this, [&game]() {
        game.dispatch();
    });
    QVERIFY(game.show(QSize(64, 80)));
    QTRY_VERIFY2(status().contains(QStringLiteral("fitted into 102 × 128 with bars")), qPrintable(status()));
    const double across = 128.0 / 102.0;
    movePointer(QPoint(20, 64));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF((20 - 13) * across, 64)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    movePointer(QPoint(100, 30));
    QTRY_VERIFY(near(game.lastMotion(), QPointF((100 - 13) * across, 30)));

    // Confined, the pointer stops at the picture's edge rather than going on
    // into the bar, where it would reach nothing of the game.
    QVERIFY(game.confinePointer());
    QTRY_VERIFY2(game.pointerConfined(), qPrintable(status()));
    movePointer(QPoint(4, 64));
    QTRY_VERIFY2(near(game.lastMotion(), QPointF(0, 64)),
                 qPrintable(QStringLiteral("%1, %2").arg(game.lastMotion().x()).arg(game.lastMotion().y())));
    movePointer(QPoint(124, 64));
    QTRY_VERIFY(near(game.lastMotion(), QPointF((114 - 13) * across, 64)));
}
