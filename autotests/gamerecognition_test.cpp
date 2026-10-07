/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What All games counts as a game: what Wine runs, what a Steam library
// holds, and what an installed desktop entry in the Game category starts. The
// entries are written to private data directories, a person's and the
// system's, which each case starts empty.

#include "gamerecognition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace KWin;

class GameRecognitionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void recognizesWineAndSteamByTheirNames();
    void recognizesWhatAGameEntryStarts();
    void passesOverWhatOtherEntriesStart();
    void passesOverInterpretersAndLaunchers();
    void recognizesSandboxedGamesByTheirSandbox();
    void recognizesTheEntryAWindowNames();
    void aPersonsEntryHidesTheSystems();
    void readsEntriesInstalledLater();

private:
    QString program(const QString &name);
    void writeEntry(const QString &directory, const QString &name, const QByteArray &body);

    std::unique_ptr<QTemporaryDir> m_root;
};

void GameRecognitionTest::init()
{
    m_root = std::make_unique<QTemporaryDir>();
    QVERIFY(m_root->isValid());
    QVERIFY(QDir(m_root->path()).mkpath(QStringLiteral("home/applications")));
    QVERIFY(QDir(m_root->path()).mkpath(QStringLiteral("system/applications")));
    QVERIFY(QDir(m_root->path()).mkpath(QStringLiteral("bin")));
    qputenv("XDG_DATA_HOME", m_root->filePath(QStringLiteral("home")).toLocal8Bit());
    qputenv("XDG_DATA_DIRS", m_root->filePath(QStringLiteral("system")).toLocal8Bit());
    qputenv("PATH", m_root->filePath(QStringLiteral("bin")).toLocal8Bit());
}

// An executable of this case's own, in the directory the search path names.
QString GameRecognitionTest::program(const QString &name)
{
    const QString path = m_root->filePath(QStringLiteral("bin/") + name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write("#!/bin/sh\n") < 0) {
        return {};
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return QFileInfo(path).canonicalFilePath();
}

void GameRecognitionTest::writeEntry(const QString &directory, const QString &name, const QByteArray &body)
{
    const QString path = m_root->filePath(directory + QStringLiteral("/applications/") + name);
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[Desktop Entry]\nType=Application\nName=Entry\n" + body) > 0);
}

void GameRecognitionTest::recognizesWineAndSteamByTheirNames()
{
    QVERIFY(upscaleRecognizedGame(QStringLiteral("/usr/lib/wine/wine64-preloader")));
    QVERIFY(upscaleRecognizedGame(QStringLiteral("wine:///home/me/prefix/Z:/games/game.exe")));
    QVERIFY(upscaleRecognizedGame(QStringLiteral("/home/me/.local/share/Steam/steamapps/common/Game/game.x86_64")));
    QVERIFY(!upscaleRecognizedGame(QStringLiteral("/usr/bin/plasmashell")));
    QVERIFY(!upscaleRecognizedGame(QString()));
}

void GameRecognitionTest::recognizesWhatAGameEntryStarts()
{
    const QString absolute = program(QStringLiteral("absolute-game"));
    const QString searched = program(QStringLiteral("searched-game"));
    const QString prepared = program(QStringLiteral("prepared-game"));
    writeEntry(QStringLiteral("system"), QStringLiteral("absolute.desktop"),
               "Categories=Game;ArcadeGame;\nExec=" + absolute.toUtf8() + " --fullscreen %U\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("searched.desktop"), "Categories=Game;\nExec=searched-game\n");
    // env and its assignments only prepare the program's environment.
    writeEntry(QStringLiteral("system"), QStringLiteral("prepared.desktop"),
               "Categories=Game;\nExec=env SDL_VIDEODRIVER=wayland \"prepared-game\" %f\n");
    QVERIFY(upscaleRecognizedGame(absolute));
    QVERIFY(upscaleRecognizedGame(searched));
    QVERIFY(upscaleRecognizedGame(prepared));
}

void GameRecognitionTest::passesOverWhatOtherEntriesStart()
{
    const QString tool = program(QStringLiteral("tool"));
    const QString launcher = program(QStringLiteral("launcher"));
    const QString link = program(QStringLiteral("link"));
    // The word is in its name, not among its categories.
    writeEntry(QStringLiteral("system"), QStringLiteral("tool.desktop"), "Name[en]=Game editor\nCategories=Utility;\nExec=tool\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("launcher.desktop"), "Categories=Game;\nExec=launcher\nHidden=true\n");
    QFile file(m_root->filePath(QStringLiteral("system/applications/link.desktop")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[Desktop Entry]\nType=Link\nName=Link\nCategories=Game;\nExec=link\nURL=https://example.org\n") > 0);
    file.close();
    QVERIFY(!upscaleRecognizedGame(tool));
    QVERIFY(!upscaleRecognizedGame(launcher));
    QVERIFY(!upscaleRecognizedGame(link));
}

void GameRecognitionTest::passesOverInterpretersAndLaunchers()
{
    // A link named like a game that resolves to a shell is the shell.
    const QString shell = QFileInfo(QStringLiteral("/bin/sh")).canonicalFilePath();
    QVERIFY(!shell.isEmpty());
    QVERIFY(QFile::link(shell, m_root->filePath(QStringLiteral("bin/linked-game"))));
    writeEntry(QStringLiteral("system"), QStringLiteral("script.desktop"), "Categories=Game;\nExec=sh -c \"exec game\"\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("python.desktop"), "Categories=Game;\nExec=python3.13 /usr/share/game/main.py\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("linked.desktop"), "Categories=Game;\nExec=linked-game\n");
    QVERIFY(!upscaleRecognizedGame(shell));
}

void GameRecognitionTest::recognizesSandboxedGamesByTheirSandbox()
{
    // Their entries start the sandbox's launcher, which is no game of its own.
    const QString launcher = program(QStringLiteral("sandbox-launcher"));
    writeEntry(QStringLiteral("system"), QStringLiteral("org.example.Game.desktop"),
               "Categories=Game;\nExec=sandbox-launcher run org.example.Game\nX-Flatpak=org.example.Game\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("racer_racer.desktop"),
               "Categories=Game;\nExec=env BAMF_DESKTOP_FILE_HINT=x sandbox-launcher racer\nX-SnapInstanceName=racer\n");
    QVERIFY(upscaleRecognizedGame(QStringLiteral("flatpak://org.example.Game/app/bin/game")));
    QVERIFY(!upscaleRecognizedGame(QStringLiteral("flatpak://org.example.Editor/app/bin/editor")));
    QVERIFY(upscaleRecognizedGame(QStringLiteral("/snap/racer/678/usr/bin/racer")));
    QVERIFY(!upscaleRecognizedGame(QStringLiteral("/snap/editor/12/usr/bin/editor")));
    QVERIFY(!upscaleRecognizedGame(launcher));
}

void GameRecognitionTest::recognizesTheEntryAWindowNames()
{
    writeEntry(QStringLiteral("system"), QStringLiteral("org.example.Puzzle.desktop"), "Categories=Game;LogicGame;\nExec=python3 puzzle.py\n");
    // Named by its path below the directory, joined by a dash.
    writeEntry(QStringLiteral("home"), QStringLiteral("games/chess.desktop"), "Categories=Game;BoardGame;\nExec=chess\n");
    writeEntry(QStringLiteral("system"), QStringLiteral("org.example.Viewer.desktop"), "Categories=Graphics;\nExec=viewer\n");
    QVERIFY(upscaleGameDesktopFile(QStringLiteral("org.example.Puzzle")));
    QVERIFY(upscaleGameDesktopFile(QStringLiteral("org.example.Puzzle.desktop")));
    QVERIFY(upscaleGameDesktopFile(QStringLiteral("games-chess")));
    QVERIFY(!upscaleGameDesktopFile(QStringLiteral("org.example.Viewer")));
    QVERIFY(!upscaleGameDesktopFile(QString()));
}

void GameRecognitionTest::aPersonsEntryHidesTheSystems()
{
    const QString game = program(QStringLiteral("hidden-game"));
    writeEntry(QStringLiteral("system"), QStringLiteral("hidden.desktop"), "Categories=Game;\nExec=hidden-game\n");
    QVERIFY(upscaleRecognizedGame(game));
    writeEntry(QStringLiteral("home"), QStringLiteral("hidden.desktop"), "Hidden=true\n");
    QVERIFY(!upscaleRecognizedGame(game));
    QVERIFY(!upscaleGameDesktopFile(QStringLiteral("hidden")));
}

// Asked again straight after an entry is written, within the tick of the
// file system's clock the directory was read in as often as not.
void GameRecognitionTest::readsEntriesInstalledLater()
{
    const QString game = program(QStringLiteral("later-game"));
    QVERIFY(!upscaleRecognizedGame(game));
    writeEntry(QStringLiteral("system"), QStringLiteral("later.desktop"), "Categories=Game;\nExec=later-game\n");
    QVERIFY(upscaleRecognizedGame(game));
    QVERIFY(QFile::remove(m_root->filePath(QStringLiteral("system/applications/later.desktop"))));
    QVERIFY(!upscaleRecognizedGame(game));
}

QTEST_GUILESS_MAIN(GameRecognitionTest)

#include "gamerecognition_test.moc"
