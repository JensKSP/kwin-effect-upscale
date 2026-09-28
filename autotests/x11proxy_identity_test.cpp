// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
#include <QCoreApplication>
#include <QDebug>
#include <stdexcept>

using namespace UpscaleX11;

namespace
{
void check(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Wine names a Windows program on a lettered drive; every other shape is a
// program of the host, including a Unix path that merely holds a colon.
void windowsPaths()
{
    check(upscaleWindowsPath(QStringLiteral("C:\\windows\\system32\\explorer.exe")), "drive path");
    check(upscaleWindowsPath(QStringLiteral("z:\\games\\game.exe")), "lower-case drive");
    check(!upscaleWindowsPath(QStringLiteral("/usr/games/extremetuxracer")), "unix path");
    check(!upscaleWindowsPath(QStringLiteral("/opt/a:b/game")), "unix path with a colon");
    check(!upscaleWindowsPath(QStringLiteral("C:\\")), "drive root is too short to name a program");
    check(!upscaleWindowsPath(QString()), "empty");
}

// A pattern is a regular expression, so separators are turned around; the
// case on disk is what a person writes a pattern against, so it is kept.
void programPaths()
{
    check(upscaleProgramPath(QStringLiteral("Z:\\home\\me\\Games\\Wreckfest\\Wreckfest.exe"))
              == QStringLiteral("Z:/home/me/Games/Wreckfest/Wreckfest.exe"),
          "separators and case");
    check(upscaleProgramPath(QStringLiteral("/usr/games/etr")) == QStringLiteral("/usr/games/etr"),
          "a host path is unchanged");
}

// Wine's own components sit on the prefix's drive below its Windows
// directory. A game does not, wherever it is installed.
void components()
{
    check(upscaleWineComponent(QStringLiteral("c:/windows/system32/explorer.exe")), "explorer");
    check(upscaleWineComponent(QStringLiteral("C:/Windows/System32/services.exe")), "mixed case");
    check(upscaleWineComponent(QStringLiteral("c:/windows/syswow64/rundll32.exe")), "syswow64");
    check(!upscaleWineComponent(QStringLiteral("Z:/home/me/Games/Wreckfest.exe")), "a game");
    // A game of its own that happens to carry the words is still a game,
    // because the test is anchored at the prefix's own drive.
    check(!upscaleWineComponent(QStringLiteral("d:/windows/system32/game.exe")), "another drive");
    check(!upscaleWineComponent(QStringLiteral("Z:/games/c:/windows/system32/x.exe")), "not at the start");
}

// The name is a URI: the prefix is a path and leaves the authority empty the
// way file:/// does, while a container identifier would occupy it instead.
void runtimeIdentities()
{
    check(upscaleRuntimeIdentity(u"wine", QStringLiteral("/home/me/.wine"), QStringLiteral("c:/windows/system32/explorer.exe"))
              == QStringLiteral("wine:///home/me/.wine/c:/windows/system32/explorer.exe"),
          "wine names its prefix in the path");
    check(upscaleRuntimeIdentity(u"docker", QStringLiteral("a1b2c3"), QStringLiteral("/usr/bin/game"))
              == QStringLiteral("docker://a1b2c3/usr/bin/game"),
          "a container names itself in the authority");
    check(upscaleRuntimeIdentity(u"wine", QString(), QStringLiteral("c:/game.exe")).isEmpty(), "no prefix");
    check(upscaleRuntimeIdentity(u"wine", QStringLiteral("/home/me/.wine"), QString()).isEmpty(), "no program");
}

// A program of the host is named by its path alone. One Wine runs is named
// for its prefix as well, with the loader behind it kept as a second name so
// that a pattern can still single out every Wine program at once.
void candidates()
{
    ProgramIdentity host;
    host.executable = QStringLiteral("/usr/games/etr");
    host.program = host.executable;
    check(host.candidates() == QStringList{QStringLiteral("/usr/games/etr")}, "one name for a host program");
    check(!host.isWine(), "no prefix is no Wine");

    ProgramIdentity game;
    game.executable = QStringLiteral("/usr/bin/wine-preloader");
    game.program = QStringLiteral("Z:/home/me/Games/Wreckfest/Wreckfest.exe");
    game.prefix = QStringLiteral("/home/me/.wine");
    check(game.isWine(), "a prefix is Wine");
    check(game.candidates()
              == QStringList{QStringLiteral("wine:///home/me/.wine/Z:/home/me/Games/Wreckfest/Wreckfest.exe"),
                             QStringLiteral("/usr/bin/wine-preloader")},
          "the program comes before the loader");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        windowsPaths();
        programPaths();
        components();
        runtimeIdentities();
        candidates();
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
    qInfo() << "PASS windows paths, separators, components, runtime names, candidate order";
    return 0;
}
