/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"
#include "x11_integration_test.h"
#include "x11_standin_game.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QScopeGuard>
#include <QTest>
#include <QVersionNumber>

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
//
// What KWin itself does with such a window changed: 6.3 withdraws it and the
// client hears so, while 6.6 leaves it mapped and says nothing, with or
// without this effect (observed 2026-09-28 on 6.6.6 with Xwayland 24.1.10).
// There is then no withdrawal for the order to be judged by, so the case
// runs where KWin still withdraws the window.
void UpscaleX11IntegrationTest::withdrawnWhileHeld()
{
    if (QVersionNumber::fromString(QStringLiteral(UPSCALE_TEST_KWIN_VERSION)) >= QVersionNumber(6, 6)) {
        QSKIP("KWin 6.6 leaves a window withdrawn right after mapping mapped, with or without the effect");
    }
    configure(true);
    X11Client target(false);
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1024, 768), false));
    target.withdraw();
    QTRY_VERIFY(target.unmapNotifies() > 0);
    // Until nothing is held: a mapping still pending would be released then.
    UPSCALE_TRY_SETTLED();
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

    // A connection that returns to native display replies can resize to the
    // physical output. Its historical smaller screen must not disqualify it.
    X11Client native(false);
    native.reportProcess();
    QVERIFY(native.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160), false));
    QVERIFY(native.waitForMapping());
    QTRY_VERIFY2(!status().contains(QStringLiteral("not fullscreen or a selected borderless window")),
                 qPrintable(status()));
}

// An entry can ask its game to confirm a resize through the X11 mode it then
// sets, as Extreme Tux Racer's does: a game resized after it started can keep
// its old viewport behind a smaller window. One whose connection the proxy
// answered starts at the smaller screen and sets no mode, and Extreme Tux
// Racer waited forever for it (KWin 6.6.6, 2026-10-07). It is presented.
void UpscaleX11IntegrationTest::presentsAServedGameThatSetsNoMode()
{
    KConfigGroup entry(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-test"));
    entry.writeEntry("X11RequiresEmulatedMode", true);
    entry.sync();
    configure(true);
    QDBusInterface policy(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    QVariantMap answer;
    QVERIFY(QTest::qWaitFor([&]() {
        const QDBusReply<QVariantMap> reply = policy.call(QStringLiteral("x11ConnectionPolicy"), uint(QCoreApplication::applicationPid()),
                                                          QStringList{QStringLiteral("upscale-x11-test")});
        answer = reply.isValid() ? reply.value() : QVariantMap{};
        return !answer.value(QStringLiteral("retry")).toBool();
    }, 10000));
    const QString reason = answer.value(QStringLiteral("reason")).toString();
    if (reason.contains(QStringLiteral("one enabled output"))) {
        QSKIP("a connection is answered only for a single screen");
    }
    const QSize given(answer.value(QStringLiteral("width")).toInt(), answer.value(QStringLiteral("height")).toInt());
    QVERIFY2(!given.isEmpty(), qPrintable(reason));
    X11Client target(false);
    target.reportProcess();
    QVERIFY(target.show(QByteArrayLiteral("upscale-x11-test"), QRect(QPoint(0, 0), given), false));
    QVERIFY(target.waitForMapping());
    target.fullscreen(true);
    QTRY_VERIFY(target.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QVERIFY2(!status().contains(QStringLiteral("has not confirmed")), qPrintable(status()));
}

void UpscaleX11IntegrationTest::winePrefixEligibility_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("change");
    QTest::addColumn<bool>("expected");
    QTest::newRow("shipped-native-catalogue") << QString() << QString() << false;
    // A pattern that could match in any prefix names a program, which is
    // asked about once a process names it, and holds no prefix meanwhile.
    QTest::newRow("wine-program") << QStringLiteral(".*/Wreckfest/Wreckfest\\.exe") << QString() << false;
    QTest::newRow("wine-program-in-prefix") << QStringLiteral("wine:///test/prefix/.*/Wreckfest\\.exe") << QString() << true;
    QTest::newRow("wine-prefix") << QStringLiteral("wine:///test/prefix/.*") << QString() << true;
    QTest::newRow("other-prefix") << QStringLiteral("wine:///other/prefix/.*") << QString() << false;
    QTest::newRow("known-loader") << QStringLiteral("/usr/bin/wine") << QString() << true;
    QTest::newRow("native-resolution") << QStringLiteral("wine://.*") << QStringLiteral("Native") << false;
    QTest::newRow("disabled-profile") << QStringLiteral("wine://.*") << QStringLiteral("Disabled") << false;
    QTest::newRow("off-presentation") << QStringLiteral("wine://.*") << QStringLiteral("Off") << false;
    QTest::newRow("below-threshold") << QStringLiteral("wine://.*") << QStringLiteral("Threshold") << false;
}

void UpscaleX11IntegrationTest::winePrefixEligibility()
{
    QFETCH(QString, pattern);
    QFETCH(QString, change);
    QFETCH(bool, expected);
    const KSharedConfig::Ptr catalogue = KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"));
    const KConfig shipped(QStringLiteral(UPSCALE_APPLICATION_DEFAULTS), KConfig::SimpleConfig);
    for (const QString &name : shipped.groupList()) {
        KConfigGroup destination(catalogue, name);
        KConfigGroup(&shipped, name).copyTo(&destination);
    }
    KConfigGroup entry(catalogue, QStringLiteral("Application-test"));
    entry.writeEntry("X11ConnectionExecutable", pattern);
    if (change == QLatin1String("Native")) {
        entry.writeEntry("Resolution", QStringLiteral("Native"));
    } else if (change == QLatin1String("Off")) {
        entry.writeEntry("MethodX11Borderless", QStringLiteral("Off"));
    } else if (change == QLatin1String("Threshold")) {
        entry.writeEntry("MinimumPixels", 3840 * 2160 + 1);
    }
    entry.sync();
    configure(change != QLatin1String("Disabled"));
    QDBusInterface policy(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    const QDBusReply<bool> answer = policy.call(QStringLiteral("x11PrefixMayMatch"), QStringLiteral("/test/prefix"),
                                                QStringList{QStringLiteral("wine:///test/prefix/C:/windows/system32/winecfg.exe"), QStringLiteral("/usr/bin/wine")});
    QVERIFY2(answer.isValid(), qPrintable(answer.error().message()));
    // The multi-output companion session cannot advertise a connection size.
    if (qEnvironmentVariableIntValue("UPSCALE_TEST_OUTPUT_COUNT") != 1) {
        expected = false;
    }
    QCOMPARE(answer.value(), expected);
}

// A window of a process shown another's screen, a Wine prefix's launcher once
// its game was answered, is claimed by the entry that answered the game
// although no entry names it, and presented at that screen: one prefix is one
// screen. Here this process is the game and the stand-in the
// launcher, fullscreen at the output's size as a launcher that became
// fullscreen before the game started.
void UpscaleX11IntegrationTest::presentsAProcessShownItsGamesScreen()
{
    configure(true);
    QDBusInterface policy(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    QVariantMap game;
    QVERIFY(QTest::qWaitFor([&]() {
        const QDBusReply<QVariantMap> reply = policy.call(QStringLiteral("x11ConnectionPolicy"), uint(QCoreApplication::applicationPid()),
                                                          QStringList{QStringLiteral("upscale-x11-test")});
        game = reply.isValid() ? reply.value() : QVariantMap{};
        return !game.value(QStringLiteral("retry")).toBool();
    }, 10000));
    if (game.value(QStringLiteral("reason")).toString().contains(QStringLiteral("one enabled output"))) {
        QSKIP("a connection is answered only for a single screen");
    }
    QCOMPARE(game.value(QStringLiteral("width")).toInt(), 1920);
    StandInGame launcher(QStringLiteral(UPSCALE_TEST_X11_GAME), QStringLiteral("on-map"), QSize(3840, 2160), false,
                         QStringLiteral("upscale-x11-launcher"));
    QVERIFY(launcher.started());
    QVERIFY(policy.call(QStringLiteral("x11ProcessShown"), uint(QCoreApplication::applicationPid()), uint(launcher.processId()))
                .type()
            != QDBusMessage::ErrorMessage);
    QTRY_VERIFY(launcher.isFullscreen());
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QTRY_COMPARE(launcher.geometry().size(), QSize(1920, 1080));
}

// Under All applications a program no entry names is told the smaller screen
// when it connects, as an entry's program is, so that it starts with the
// viewport it keeps. Nothing is said with All applications off, nor
// yet for a program an entry names without a connection pattern, which that
// entry decides once the window exists.
void UpscaleX11IntegrationTest::answersUnlistedProgramsUnderAllApplications()
{
    KConfigGroup other(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-other"));
    other.writeEntry("Name", QStringLiteral("Other"));
    other.writeEntry("Executable", QStringLiteral(".*/other-game"));
    other.writeEntry("ExecutableMatch", QStringLiteral("RegularExpression"));
    other.sync();
    const auto allApplications = [this](bool on) {
        KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
        group.writeEntry("UnlistedApplications", on);
        group.sync();
        configure(true);
    };
    // The entry goes with the case, or every later case of the session would
    // run with it enabled.
    const auto restore = qScopeGuard([&allApplications, &other]() {
        other.deleteGroup();
        other.sync();
        allApplications(false);
    });
    QDBusInterface policy(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QDBusConnection::sessionBus());
    const auto ask = [&policy](const QString &program) {
        const QDBusReply<QVariantMap> reply =
            policy.call(QStringLiteral("x11ConnectionPolicy"), uint(QCoreApplication::applicationPid()), QStringList{program});
        return reply.isValid() ? reply.value() : QVariantMap{{QStringLiteral("reason"), reply.error().message()}};
    };
    allApplications(false);
    QCOMPARE(ask(QStringLiteral("/usr/games/unlisted-game")).value(QStringLiteral("reason")).toString(),
             QStringLiteral("not in the list, and All applications is off"));
    allApplications(true);
    // Until KWin has read Xwayland's modes the answer is to ask again, which
    // the proxy does within its bounded wait.
    QVariantMap unlisted;
    QVERIFY(QTest::qWaitFor([&]() {
        unlisted = ask(QStringLiteral("/usr/games/unlisted-game"));
        return !unlisted.value(QStringLiteral("retry")).toBool();
    }, 10000));
    const QString reason = unlisted.value(QStringLiteral("reason")).toString();
    if (reason.contains(QStringLiteral("one enabled output"))) {
        QSKIP("a connection is answered only for a single screen");
    }
    QCOMPARE(reason, QStringLiteral("connection display advertisement"));
    QCOMPARE(unlisted.value(QStringLiteral("profile")).toString(), QStringLiteral("global"));
    QCOMPARE(QSize(unlisted.value(QStringLiteral("width")).toInt(), unlisted.value(QStringLiteral("height")).toInt()),
             QSize(1920, 1080));
    // Balanced wishes for 2259 × 1271, which Xwayland lists no mode for: the
    // program is told the listed mode nearest to it, as the resize asks it.
    configure(true, Stored::Balanced);
    const QVariantMap balanced = ask(QStringLiteral("/usr/games/unlisted-game"));
    QCOMPARE(QSize(balanced.value(QStringLiteral("width")).toInt(), balanced.value(QStringLiteral("height")).toInt()),
             QSize(2048, 1152));
    configure(true);
    QCOMPARE(ask(QStringLiteral("/usr/games/other-game")).value(QStringLiteral("reason")).toString(),
             QStringLiteral("unidentified client"));
    const QDBusReply<bool> prefix = policy.call(QStringLiteral("x11PrefixMayMatch"), QStringLiteral("/unnamed/prefix"),
                                                QStringList{QStringLiteral("wine:///unnamed/prefix/C:/windows/system32/winecfg.exe"),
                                                            QStringLiteral("/usr/bin/wine")});
    QVERIFY2(prefix.isValid() && prefix.value(), qPrintable(prefix.error().message()));
}

// A program that leaves _NET_WM_PID unset is known only by the connection that
// made its window. KWin 6.6 takes its process from there once it manages the
// window, and the effect's hold of its first mapping has to find it the same
// way beforehand: held, the fullscreen request made right after mapping is
// answered with the smaller size inside the mapping, so that the window is
// already that size when it becomes visible, which is what SFML needs. KWin 6.3 reads _NET_WM_PID alone and
// cannot tell whose such a window is at all, so the case runs from 6.6.
void UpscaleX11IntegrationTest::anUnnamedProgramIsHeldAtItsFirstMapping()
{
    if (QVersionNumber::fromString(QStringLiteral(UPSCALE_TEST_KWIN_VERSION)) < QVersionNumber(6, 6)) {
        QSKIP("KWin 6.3 cannot tell whose an X11 window without _NET_WM_PID is");
    }
    QFile catalogue(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(catalogue.open(QIODevice::Append));
    QVERIFY(catalogue.write("[Application-unnamed]\nName=Unnamed game\nExecutable=.*/upscale_test_x11_game\n"
                            "ExecutableMatch=RegularExpression\nMethodX11FullScreen=X11Resize\n")
            > 0);
    catalogue.close();
    KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"))->reparseConfiguration();
    configure(false);
    StandInGame game(QStringLiteral(UPSCALE_TEST_X11_GAME), QStringLiteral("after-map"), QSize(1024, 768), true,
                     QStringLiteral("unnamed-x11-game"));
    QVERIFY(game.started());
    QTRY_VERIFY_WITH_TIMEOUT(game.sizeAtMapping().isValid(), 15000);
    QCOMPARE(game.sizeAtMapping(), QSize(1920, 1080));
}
