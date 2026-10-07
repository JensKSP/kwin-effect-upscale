/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "gamerecognition.h"

#include "runtime.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <optional>
#include <utility>

namespace KWin
{

namespace
{

// What the installed desktop entries in the Game category name, and the
// directories they were read from, as they were when they were read.
struct GameIndex
{
    // The directories that changed after this may change again unseen.
    QDateTime settled;
    QStringList locations;
    QList<std::pair<QString, QDateTime>> directories;
    QSet<QString> desktopFiles;
    QSet<QString> programs;
    QSet<QString> flatpaks;
    QSet<QString> snaps;
};

}

static const QLatin1String desktopSuffix(".desktop");

// Programs that run what they are handed rather than being it. An entry that
// starts its game through one names a process that runs other programs as
// well, so what it names says nothing about this one.
static bool runsOthers(const QString &path)
{
    // Named with the version some systems install them under, python3.13 say.
    static const QRegularExpression s_interpreter(
        QStringLiteral("^(?:sh|bash|dash|zsh|ksh|fish|perl|ruby|node|java|mono|python|lua|luajit)[0-9.]*$"));
    return s_interpreter.match(QFileInfo(path).fileName()).hasMatch();
}

// The program an Exec line starts, as the system resolves it, or empty. env
// and the assignments it is handed only prepare that program's environment.
static QString execProgram(const QString &exec)
{
    static const QRegularExpression s_assignment(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*="));
    const QStringList words = QProcess::splitCommand(exec);
    for (const QString &word : words) {
        if (QFileInfo(word).fileName() == QLatin1String("env") || s_assignment.match(word).hasMatch()) {
            continue;
        }
        if (runsOthers(word)) {
            return {};
        }
        QString found = QDir::isAbsolutePath(word) ? word : QStandardPaths::findExecutable(word);
        if (found.isEmpty()) {
            // Where distributions put games, which a desktop session's own
            // search path need not include.
            found = QStandardPaths::findExecutable(word, {QStringLiteral("/usr/games"), QStringLiteral("/usr/local/games")});
        }
        // A process is known by the path its executable resolves to.
        const QString program = QFileInfo(found).canonicalFilePath();
        return runsOthers(program) ? QString() : program;
    }
    return {};
}

static void readEntry(GameIndex &index, const QString &id, const QString &path)
{
    // Most entries are not games, and parsing one costs more than looking
    // for the word.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.readAll().contains("Game")) {
        return;
    }
    file.close();
    const KDesktopFile entry(path);
    const KConfigGroup group = entry.desktopGroup();
    if (!entry.hasApplicationType() || group.readEntry("Hidden", false)
        || !group.readXdgListEntry("Categories").contains(QLatin1String("Game"))) {
        return;
    }
    index.desktopFiles.insert(id);
    // A sandboxed application's entry starts the sandbox's launcher, which
    // starts every application of its kind; the sandbox names its own.
    if (const QString flatpak = group.readEntry("X-Flatpak"); !flatpak.isEmpty()) {
        index.flatpaks.insert(flatpak);
    } else if (const QString snap = group.readEntry("X-SnapInstanceName"); !snap.isEmpty()) {
        index.snaps.insert(snap);
    } else if (const QString program = execProgram(group.readEntry("Exec")); !program.isEmpty()) {
        index.programs.insert(program);
    }
}

// Entries are named by their path below the directory they are installed
// in, a directory's name joined to its file's by a dash, and an entry of a
// directory read earlier hides one of the same name read later, a hidden one
// included: that is how a person's own entries replace the system's.
static void readDirectory(GameIndex &index, QSet<QString> &seen, const QDir &root, const QString &directory)
{
    // A file system keeps a change's time at a granularity of its own: a tick
    // of a coarse clock on Linux, a second or two on some others. A directory
    // changed just before it was read can change again within that tick and
    // keep its time, so its time is not kept, and it is read again when next
    // asked.
    const QDateTime modified = QFileInfo(directory).lastModified();
    index.directories.append({directory, modified < index.settled ? modified : QDateTime()});
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            // An entry may be a link, as Flatpak's are; a directory that is
            // one could lead back to where it is.
            if (!info.isSymLink()) {
                readDirectory(index, seen, root, info.filePath());
            }
            continue;
        }
        if (!info.fileName().endsWith(desktopSuffix)) {
            continue;
        }
        QString id = root.relativeFilePath(info.filePath());
        id.chop(desktopSuffix.size());
        id.replace(QLatin1Char('/'), QLatin1Char('-'));
        if (!seen.contains(id)) {
            seen.insert(id);
            readEntry(index, id, info.filePath());
        }
    }
}

// Whether a directory still has the time it had when it was read.
static bool unchanged(const std::pair<QString, QDateTime> &directory)
{
    return QFileInfo(directory.first).lastModified() == directory.second;
}

static const GameIndex &gameIndex()
{
    static std::optional<GameIndex> s_index;
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    const bool current = s_index && s_index->locations == locations && std::ranges::all_of(s_index->directories, unchanged);
    if (!current) {
        GameIndex index;
        index.settled = QDateTime::currentDateTimeUtc().addSecs(-2);
        index.locations = locations;
        QSet<QString> seen;
        for (const QString &location : locations) {
            readDirectory(index, seen, QDir(location), location);
        }
        s_index = std::move(index);
    }
    return *s_index;
}

// The segment of @p name after @p scheme, up to the next slash.
static QString segmentAfter(const QString &name, QLatin1String scheme)
{
    const qsizetype end = name.indexOf(QLatin1Char('/'), scheme.size());
    return name.mid(scheme.size(), end < 0 ? -1 : end - scheme.size());
}

bool upscaleRecognizedGame(const QString &program)
{
    // On this desktop what Wine runs is a Windows game. The session proxy
    // names a program Wine runs by its prefix, and KWin names its window's
    // process by Wine's loader.
    if (program.startsWith(QLatin1String("wine://")) || upscaleWineRuntime(program)
        || program.contains(QLatin1String("/steamapps/common/"))) {
        return true;
    }
    if (program.isEmpty()) {
        return false;
    }
    const GameIndex &index = gameIndex();
    if (const QLatin1String flatpak("flatpak://"); program.startsWith(flatpak)) {
        return index.flatpaks.contains(segmentAfter(program, flatpak));
    }
    if (const QLatin1String snap("/snap/"); program.startsWith(snap)) {
        return index.snaps.contains(segmentAfter(program, snap));
    }
    return index.programs.contains(program);
}

bool upscaleGameDesktopFile(const QString &desktopFileName)
{
    QString id = QFileInfo(desktopFileName).fileName();
    if (id.endsWith(desktopSuffix)) {
        id.chop(desktopSuffix.size());
    }
    return !id.isEmpty() && gameIndex().desktopFiles.contains(id);
}

} // namespace KWin
