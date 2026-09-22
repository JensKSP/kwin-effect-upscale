/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QTest>

// Stands in for a helper answering org.kde.KWin.Upscale.Helper1: it prepared
// every program it is asked about to render at `size`.
class TestHelper : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Upscale.Helper1")

public:
    QSize size;
    QList<uint> asked;

public Q_SLOTS:
    Q_SCRIPTABLE int present(uint pid, const QString &windowClass, int &height)
    {
        Q_UNUSED(windowClass)
        asked.append(pid);
        height = size.height();
        return size.width();
    }
};

/*
 * A window whose program a helper prepared to render smaller: an ordinary
 * window at that size, which the effect makes fullscreen, holds at its size
 * and presents across the output.
 */
class UpscaleX11PreparedTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void presentsAWindowAHelperPrepared();
    void leavesAWindowNobodyPrepared();

private:
    QString status();
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};

QString UpscaleX11PreparedTest::status()
{
    const QDBusReply<QString> reply = m_effects.call(QStringLiteral("supportInformation"), QStringLiteral("upscale_test_driver"));
    return reply.isValid() ? reply.value() : reply.error().message();
}

void UpscaleX11PreparedTest::init()
{
    QTRY_VERIFY(m_effects.isValid());
    // The effect only asks about a program it acts on.
    QFile catalogue(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(catalogue.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(catalogue.write("[Application-test]\nName=X11 test\nWindowClass=upscale-x11-test\nEnabled=true\n") > 0);
    catalogue.close();
    KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"))->reparseConfiguration();
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Osd", false);
    group.writeEntry("MinimumPixels", 1920 * 1080);
    group.sync();
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
}

void UpscaleX11PreparedTest::cleanup()
{
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

void UpscaleX11PreparedTest::presentsAWindowAHelperPrepared()
{
    TestHelper helper;
    helper.size = QSize(1920, 1080);
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerObject(QStringLiteral("/Helper"), &helper, QDBusConnection::ExportScriptableSlots));
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin.Upscale.Helper")));

    // A Wine virtual desktop is an ordinary decorated window of the prepared
    // size, and its program never asks for fullscreen.
    X11Client game(false);
    game.reportProcess();
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(100, 100, 1920, 1080), false));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);
    QCOMPARE(helper.asked.value(0), uint(QCoreApplication::applicationPid()));
    QCOMPARE(game.geometry().size(), QSize(1920, 1080));
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Supplied input: 1920 × 1080")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Destination: 3840 × 2160")), qPrintable(status()));

    // Taking it out of fullscreen gives it back to its user for good.
    game.fullscreen(false);
    QTRY_VERIFY_WITH_TIMEOUT(!game.isFullscreen(), 10000);
    QTest::qWait(1000);
    QVERIFY(!game.isFullscreen());
    QCOMPARE(game.geometry().size(), QSize(1920, 1080));

    bus.unregisterObject(QStringLiteral("/Helper"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin.Upscale.Helper")));
}

void UpscaleX11PreparedTest::leavesAWindowNobodyPrepared()
{
    // Without a helper nobody answers, and the window stays as it is.
    X11Client window(false);
    QVERIFY(window.show(QByteArrayLiteral("upscale-x11-test"), QRect(100, 100, 1920, 1080), false));
    QTest::qWait(2000);
    QVERIFY(!window.isFullscreen());
    QCOMPARE(window.geometry().size(), QSize(1920, 1080));
}

QTEST_MAIN(UpscaleX11PreparedTest)

#include "x11_prepared_test.moc"
