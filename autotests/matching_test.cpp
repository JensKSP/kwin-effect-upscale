/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// How a profile is found: the program's path and the window's identity, each
// compared exactly, as a substring or as a regular expression. It runs inside
// application_test.cpp's main, in the configuration directories that prepares,
// against the catalogue this build ships.

#include "application.h"
#include "matching.h"

#include <QFile>
#include <QTest>

#include <algorithm>
#include <ranges>

using namespace KWin;

static const UpscaleApplication *forWindow(const QString &executable, const QString &windowClass, const QString &instance)
{
    return upscaleApplicationFor({executable, windowClass, instance});
}

static const UpscaleApplication *atBind(const QString &executable)
{
    return upscaleApplicationAtBind(executable).application;
}

class MatchingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void matchesProgramsByPath();
    void requiresEveryStatedGate();
    void comparesByEachMatchType();
    void refusesPatternsThatCannotBeUsed();

private:
    static void writeUserConfig(const QString &contents);
};

static QString userFile()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QLatin1String("/kwinupscalerc");
}

void MatchingTest::init()
{
    QFile::remove(userFile());
    upscaleReloadApplications();
}

void MatchingTest::writeUserConfig(const QString &contents)
{
    QFile file(userFile());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents.toUtf8()), qint64(contents.toUtf8().size()));
    file.close();
    upscaleReloadApplications();
}

void MatchingTest::matchesProgramsByPath()
{
    const UpscaleApplication *kart = forWindow(QStringLiteral("/usr/games/supertuxkart"), QStringLiteral("supertuxkart"),
                                               QStringLiteral("supertuxkart"));
    QVERIFY(kart);
    // The same game sits in different directories depending on how it was
    // installed, so the shipped entry states the file name in any of them.
    QCOMPARE(atBind(QStringLiteral("/usr/games/supertuxkart")), kart);
    QCOMPARE(atBind(QStringLiteral("/usr/local/bin/supertuxkart")), kart);
    QCOMPARE(forWindow(QStringLiteral("/home/player/SuperTuxKart/bin/supertuxkart"), QString(), QString()), kart);
    QVERIFY(!atBind(QString()));
    QVERIFY(!atBind(QStringLiteral("/usr/games/")));
    // A directory that merely contains the name is not the program, and a
    // pattern has to match the whole path, not a part of it.
    QVERIFY(!atBind(QStringLiteral("/opt/supertuxkart/launcher")));
    QVERIFY(!atBind(QStringLiteral("/usr/games/supertuxkart-old")));
    // Entries that state a window alone say nothing about paths, so they
    // neither answer at bind nor hold a program back: this one is unlisted.
    const UpscaleBindAnswer unlisted = upscaleApplicationAtBind(QStringLiteral("/usr/games/etr"));
    QVERIFY(unlisted.decided);
    QVERIFY(!unlisted.application);
}

// The narrow extreme: one copy of one game, among several that share a
// program name and a window identity. Every native Source game is hl2_linux.
void MatchingTest::requiresEveryStatedGate()
{
    writeUserConfig(QStringLiteral("[Application-portal2]\nName=Portal 2\n"
                                   "Executable=/games/common/Portal 2/hl2_linux\n"
                                   "WindowClass=hl2_linux\nOrder=5\n"));
    const UpscaleApplication *portal = forWindow(QStringLiteral("/games/common/Portal 2/hl2_linux"), QStringLiteral("hl2_linux"),
                                                 QStringLiteral("hl2_linux"));
    QVERIFY(portal);
    QCOMPARE(portal->id, QStringLiteral("portal2"));
    // Left 4 Dead 2 fails gate 1 and is the shipped entry's, which states the
    // game's folder in any Steam library.
    for (const QString &library : {QStringLiteral("/games/common"), QStringLiteral("/home/u/.local/share/Steam/steamapps/common"),
                                   QStringLiteral("/mnt/Steam Library/steamapps/common")}) {
        const UpscaleApplication *other = forWindow(library + QStringLiteral("/Left 4 Dead 2/hl2_linux"),
                                                    QStringLiteral("hl2_linux"), QStringLiteral("hl2_linux"));
        QVERIFY2(other, qPrintable(library));
        QCOMPARE(other->id, QStringLiteral("left4dead2"));
    }
    // Any other Source game is nobody's: the engine binary names no game.
    QVERIFY(!forWindow(QStringLiteral("/games/common/Half-Life 2/hl2_linux"), QStringLiteral("hl2_linux"),
                       QStringLiteral("hl2_linux")));
    // Nor is a window whose PID did not resolve to a path, now that the shipped
    // entry states its path.
    QVERIFY(!forWindow(QString(), QStringLiteral("hl2_linux"), QStringLiteral("hl2_linux")));
    // Its path with another window fails gate 2.
    QVERIFY(!forWindow(QStringLiteral("/games/common/Portal 2/hl2_linux"), QStringLiteral("launcher"), QString()));
    // Before the window exists only gate 1 can be checked. It matches, but the
    // entry also names a window, so the path alone does not decide: nothing
    // is advertised, and the program is not unlisted either.
    const UpscaleBindAnswer bind = upscaleApplicationAtBind(QStringLiteral("/games/common/Portal 2/hl2_linux"));
    QVERIFY(!bind.decided);
    QVERIFY(!bind.application);
    // So an advertisement in it could never be sent, which the editor says;
    // without one there is nothing to say.
    QVERIFY(upscaleAdvertisementProblem(*portal).isEmpty());
    UpscaleApplication advertising = *portal;
    advertising.methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)] = KWin::UpscaleMethod::AdvertisedMode;
    QVERIFY(!upscaleAdvertisementProblem(advertising).isEmpty());
    advertising.windowClass.clear();
    QVERIFY(upscaleAdvertisementProblem(advertising).isEmpty());
}

void MatchingTest::comparesByEachMatchType()
{
    for (const UpscaleStringMatch match :
         {UpscaleStringMatch::Exact, UpscaleStringMatch::Substring, UpscaleStringMatch::RegularExpression}) {
        QCOMPARE(upscaleStringMatchFromKey(upscaleStringMatchKey(match)), match);
    }
    // Absent, or a name from a later version, is the narrowest reading.
    QCOMPARE(upscaleStringMatchFromKey(QString()), UpscaleStringMatch::Exact);
    QCOMPARE(upscaleStringMatchFromKey(QStringLiteral("Glob")), UpscaleStringMatch::Exact);

    writeUserConfig(QStringLiteral("[Application-left4dead]\nExecutable=Left 4 Dead 2\nExecutableMatch=Substring\nOrder=1\n"
                                   "[Application-racer]\nWindowClass=Extreme Tux Racer .*\nWindowClassMatch=RegularExpression\n"
                                   "Instance=etr\nOrder=2\n"
                                   "[Application-exact]\nExecutable=/opt/exact/game\nOrder=3\n"));
    QCOMPARE(forWindow(QStringLiteral("/games/common/Left 4 Dead 2/hl2_linux"), QString(), QString())->id,
             QStringLiteral("left4dead"));
    QCOMPARE(forWindow(QString(), QStringLiteral("Extreme Tux Racer 0.8.4"), QStringLiteral("etr"))->id, QStringLiteral("racer"));
    // Anchored: the class has to be matched whole, not found inside another.
    QCOMPARE(forWindow(QString(), QStringLiteral("Not Extreme Tux Racer 1"), QStringLiteral("etr"))->id,
             QStringLiteral("extremetuxracer"));
    QCOMPARE(atBind(QStringLiteral("/opt/exact/game"))->id, QStringLiteral("exact"));
    QVERIFY(!atBind(QStringLiteral("/opt/exact/game2")));
}

// An invalid pattern would match nothing without saying so, and one that
// matches the empty string would match everything. Either entry is kept, so
// the editor can show it, and never matches.
void MatchingTest::refusesPatternsThatCannotBeUsed()
{
    writeUserConfig(QStringLiteral("[Application-broken]\nExecutable=(unclosed\nExecutableMatch=RegularExpression\nOrder=1\n"
                                   "[Application-everything]\nWindowClass=.*\nWindowClassMatch=RegularExpression\nOrder=2\n"));
    const std::vector<UpscaleApplication> &list = upscaleApplications();
    const auto broken = std::ranges::find(list, QStringLiteral("broken"), &UpscaleApplication::id);
    const auto everything = std::ranges::find(list, QStringLiteral("everything"), &UpscaleApplication::id);
    QVERIFY(broken != list.end() && everything != list.end());
    QVERIFY(!upscaleIdentityProblem(*broken).isEmpty());
    QVERIFY(!upscaleIdentityProblem(*everything).isEmpty());
    QVERIFY(!atBind(QStringLiteral("(unclosed")));
    QVERIFY(!forWindow(QStringLiteral("/usr/bin/anything"), QStringLiteral("anything"), QStringLiteral("anything")));
    // An empty window class is not a match for a pattern either.
    QVERIFY(!forWindow(QString(), QString(), QStringLiteral("x")));
}

int runMatchingTest(int argc, char *argv[])
{
    MatchingTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "matching_test.moc"
