// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "x11proxy_session_test.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTest>

#include <csignal>
#include <sys/wait.h>

using namespace UpscaleX11Test;

// Wine's own components come up before the game a prefix was started for and
// are answered for the program the prefix runs. Once every connection of the
// prefix has closed it has stopped, and the next game started in it may be
// another one: a component of that run is answered for that game, not for the
// one before it.
void ProxySessionTest::forgetsWhatAPrefixRanOnceItStops()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X10"));
    QVERIFY(session);
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("prefix")));
    QVERIFY(succeeded(spawnWine("C:\\Games\\First.exe", {"--connect", m_path}, prefix)));
    QVERIFY(m_effect.lastCandidates.join(QLatin1Char(' ')).contains(QStringLiteral("First.exe")));

    const pid_t second = spawnWine("C:\\Games\\Second.exe", {"--wait"}, prefix);
    QVERIFY(second > 0);
    const auto stop = qScopeGuard([second]() {
        kill(second, SIGTERM);
        waitpid(second, nullptr, 0);
    });
    QVERIFY(succeeded(spawnWine("C:\\windows\\system32\\explorer.exe", {"--connect", m_path}, prefix)));
    const QString names = m_effect.lastCandidates.join(QLatin1Char(' '));
    QVERIFY2(names.contains(QStringLiteral("Second.exe")), qPrintable(names));
    QVERIFY2(!names.contains(QStringLiteral("First.exe")), qPrintable(names));
}

void ProxySessionTest::unselectedWineComponentConnectsPromptly()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X11"));
    QVERIFY(session);
    m_effect.prefixMayMatch = false;
    const int before = m_effect.asked;
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("unselected")));
    // No game exists in this prefix. This must finish before the ten-second
    // identity deadline, without requesting an actual display advertisement.
    QVERIFY(succeeded(spawnWine("C:\\windows\\system32\\winecfg.exe", {"--connect", m_path}, prefix)));
    QCOMPARE(m_effect.asked, before);
}

void ProxySessionTest::selectedWineComponentWaitsForProgram()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X12"));
    QVERIFY(session);
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("selected")));
    const pid_t component = spawnWine("C:\\windows\\system32\\explorer.exe", {"--connect", m_path}, prefix);
    QVERIFY(component > 0);
    const int before = m_effect.asked;
    // The session says when it holds a connection for its prefix's program;
    // until then it has not asked anything either.
    s_logged.clear();
    s_passOn = qInstallMessageHandler(record);
    const auto passOn = qScopeGuard([]() {
        qInstallMessageHandler(s_passOn);
    });
    QTRY_VERIFY(s_logged.join(QLatin1Char('\n')).contains(QStringLiteral("waiting for the program of prefix")));
    QCOMPARE(m_effect.asked, before);
    const pid_t game = spawnWine("C:\\Games\\Delayed.exe", {"--wait"}, prefix);
    QVERIFY(game > 0);
    const auto stop = qScopeGuard([game]() {
        kill(game, SIGTERM);
        waitpid(game, nullptr, 0);
    });
    QVERIFY(succeeded(component));
    QCOMPARE(m_effect.asked, before + 1);
    QVERIFY(m_effect.lastCandidates.join(QLatin1Char(' ')).contains(QStringLiteral("Delayed.exe")));
}

// Wine started with a program's Unix path keeps that path in the command line
// rather than a Windows one, and names the program on the drive whose
// directory holds it most closely: Z:, which a prefix maps to /, for a game
// anywhere, and C: for one inside the prefix's own drive. Found 2026-09-29:
// such a program was never identified, and its prefix's connections were
// held for their ten seconds.
void ProxySessionTest::identifiesAProgramStartedByItsUnixPath()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X14"));
    QVERIFY(session);
    const QString prefix = m_directory.filePath(QStringLiteral("unix"));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/dosdevices")));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/drive_c/Games")));
    QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("Games"))));
    QVERIFY(QFile::link(QStringLiteral("../drive_c"), prefix + QStringLiteral("/dosdevices/c:")));
    QVERIFY(QFile::link(QStringLiteral("/"), prefix + QStringLiteral("/dosdevices/z:")));
    const QString canonical = QFileInfo(prefix).canonicalFilePath();
    const QString elsewhere = QFileInfo(m_directory.filePath(QStringLiteral("Games"))).canonicalFilePath();
    QVERIFY(succeeded(spawnWine(QFile::encodeName(elsewhere + QStringLiteral("/Elsewhere.exe")), {"--connect", m_path}, QFile::encodeName(canonical))));
    QVERIFY2(m_effect.lastCandidates.contains(QStringLiteral("wine://") + canonical + QStringLiteral("/Z:") + elsewhere + QStringLiteral("/Elsewhere.exe")),
             qPrintable(m_effect.lastCandidates.join(QLatin1Char(' '))));
    QVERIFY(succeeded(spawnWine(QFile::encodeName(canonical + QStringLiteral("/drive_c/Games/Inside.exe")), {"--connect", m_path}, QFile::encodeName(canonical))));
    QVERIFY2(m_effect.lastCandidates.contains(QStringLiteral("wine://") + canonical + QStringLiteral("/C:/Games/Inside.exe")),
             qPrintable(m_effect.lastCandidates.join(QLatin1Char(' '))));
}

void ProxySessionTest::aLauncherNamesTheProgramBeforeItStarts_data()
{
    QTest::addColumn<QByteArray>("loader");
    QTest::addColumn<QByteArray>("game");
    // As Proton runs every game, steam.exe and the game's Unix path, after
    // Wine has written the Windows name into the command line.
    QTest::newRow("started") << QByteArray() << QByteArrayLiteral("unix");
    // The same a moment earlier, while Wine's loader still comes first.
    QTest::newRow("starting") << QByteArrayLiteral("/opt/proton/files/lib/wine/x86_64-unix/wine") << QByteArrayLiteral("unix");
    // Windows' long-path form, which Proton starts its own helpers with.
    QTest::newRow("long-path") << QByteArray() << QByteArrayLiteral("long");
}

// Proton starts a game as `steam.exe <the game's Unix path>`. The desktop,
// explorer.exe, comes up after that launcher and before the game, and the game
// waits for the desktop: holding the desktop until the game appeared held both
// for the full ten seconds, after which Wreckfest never connected at all (wzpc,
// 2026-10-03). The launcher already names the game, so the desktop is answered
// for it at once, under the name the game itself is matched by.
void ProxySessionTest::aLauncherNamesTheProgramBeforeItStarts()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    QFETCH(QByteArray, loader);
    QFETCH(QByteArray, game);
    const auto session = startSession(QStringLiteral("X16-") + QString::fromLatin1(QTest::currentDataTag()));
    QVERIFY(session);
    // A Steam library on a drive of its own, as Proton maps one.
    const QString prefix = m_directory.filePath(QStringLiteral("proton-") + QString::fromLatin1(QTest::currentDataTag()));
    const QString library = m_directory.filePath(QStringLiteral("library"));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/dosdevices")));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/drive_c")));
    QVERIFY(QDir().mkpath(library + QStringLiteral("/steamapps/common/Game")));
    const QString canonicalLibrary = QFileInfo(library).canonicalFilePath();
    QVERIFY(QFile::link(QStringLiteral("../drive_c"), prefix + QStringLiteral("/dosdevices/c:")));
    QVERIFY(QFile::link(QStringLiteral("/"), prefix + QStringLiteral("/dosdevices/z:")));
    QVERIFY(QFile::link(canonicalLibrary, prefix + QStringLiteral("/dosdevices/s:")));
    const QString canonical = QFileInfo(prefix).canonicalFilePath();
    const QByteArray named = game == "long" ? QByteArrayLiteral("\\\\?\\S:\\steamapps\\common\\Game\\Game.exe")
                                            : QFile::encodeName(canonicalLibrary + QStringLiteral("/steamapps/common/Game/Game.exe"));
    QList<QByteArray> arguments{named, "--wait"};
    QByteArray program = "C:\\windows\\system32\\steam.exe";
    if (!loader.isEmpty()) {
        arguments.prepend(program);
        program = loader;
    }
    const pid_t launcher = spawnWine(program, arguments, QFile::encodeName(canonical));
    QVERIFY(launcher > 0);
    // As the effect answers for a pattern that names a program and no prefix:
    // the desktop is not worth holding, and is answered for the game anyway.
    m_effect.prefixMayMatch = false;
    const auto stop = qScopeGuard([launcher]() {
        kill(launcher, SIGTERM);
        waitpid(launcher, nullptr, 0);
    });
    s_logged.clear();
    s_passOn = qInstallMessageHandler(record);
    const auto passOn = qScopeGuard([]() {
        qInstallMessageHandler(s_passOn);
    });
    QVERIFY(succeeded(spawnWine("C:\\windows\\system32\\explorer.exe", {"/desktop", "--connect", m_path}, QFile::encodeName(canonical))));
    const QString expected = QStringLiteral("wine://") + canonical + QStringLiteral("/S:/steamapps/common/Game/Game.exe");
    QVERIFY2(m_effect.lastCandidates.contains(expected), qPrintable(m_effect.lastCandidates.join(QLatin1Char(' '))));
    QVERIFY2(!s_logged.join(QLatin1Char('\n')).contains(QStringLiteral("waiting for the program of prefix")), qPrintable(s_logged.join(QLatin1Char('\n'))));
}

// A prefix that runs already when a selected program starts in it - a launcher
// first, or Wine's own tools - has its earlier connections shown that
// program's screen as well, those a process opened after its first included,
// and no other prefix's. Found 2026-09-29: the program was answered while
// Wine already knew the screen at full size.
void ProxySessionTest::aWarmPrefixIsShownTheLaterScreen()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X15"));
    QVERIFY(session);
    m_effect.unselected = QStringLiteral("Launcher.exe");
    s_logged.clear();
    s_passOn = qInstallMessageHandler(record);
    const auto passOn = qScopeGuard([]() {
        qInstallMessageHandler(s_passOn);
    });
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("warm")));
    const pid_t launcher = spawnWine("C:\\Games\\Launcher.exe", {"--hold", m_path}, prefix);
    const pid_t elsewhere = spawnWine("C:\\Games\\Launcher.exe", {"--hold", m_path}, QFile::encodeName(m_directory.filePath(QStringLiteral("cold"))));
    QVERIFY(launcher > 0 && elsewhere > 0);
    const auto stop = qScopeGuard([launcher, elsewhere]() {
        for (const pid_t pid : {launcher, elsewhere}) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    });
    // Both launchers answered, at the size they have, before the game starts.
    QTRY_COMPARE(s_logged.filter(QStringLiteral("not in the list")).size(), 2);
    m_effect.shown.clear();
    QVERIFY(succeeded(spawnWine("C:\\Games\\Game.exe", {"--connect", m_path}, prefix)));
    // Once the effect has taken its report of the launcher, as it has to
    // before Wine hears of the smaller screen.
    QTRY_VERIFY2(s_logged.join(QLatin1Char('\n')).contains(QStringLiteral("now shows 2 earlier connections QSize(2560, 1440)")),
                 qPrintable(s_logged.join(QLatin1Char('\n'))));
    // The effect is told, so that it presents the launcher's windows as the
    // game's; and a program the prefix starts later is shown the game's screen
    // too, although not in the list: one prefix is one screen.
    QTRY_VERIFY(m_effect.shown.contains(uint(launcher)));
    QVERIFY(!m_effect.shown.contains(uint(elsewhere)));
    const pid_t later = spawnWine("C:\\Games\\Launcher.exe", {"--connect", m_path}, prefix);
    QVERIFY(succeeded(later));
    QTRY_VERIFY(m_effect.shown.contains(uint(later)));
}
