/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// Running the glmark2 that tools/prepare-crash-game.py builds, for the cases
// about a game that disappears without warning. See glmark2_crash.h for the
// moments it can crash at.

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Where that glmark2 was prepared, or nothing when it was not: the cases that
// need it then skip.
inline QString crashGame()
{
    return qEnvironmentVariable("UPSCALE_TEST_CRASH_GAME");
}

// Runs the crashing glmark2 of the given flavour, fullscreen, until it goes
// down at @p point, and returns the line it wrote as it did: which moment, and
// for Wayland the mode it had been told. Anything else comes back as what went
// wrong instead, for the comparison to show.
inline QString crashAt(const QString &flavour, const QString &point)
{
    QProcess game;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("UPSCALE_TEST_CRASH"), point);
    game.setProcessEnvironment(environment);
    // Merged, so that when it does not crash, the reason it gave is shown too.
    game.setProcessChannelMode(QProcess::MergedChannels);
    game.start(crashGame() + QStringLiteral("/build/src/") + flavour,
               {QStringLiteral("--fullscreen"), QStringLiteral("--data-path"), crashGame() + QStringLiteral("/data"),
                QStringLiteral("--benchmark"), QStringLiteral("clear")});
    // Bounded, not paced: this returns the moment the game is gone.
    if (!game.waitForFinished(120000)) {
        game.kill();
        game.waitForFinished();
        return QStringLiteral("still running at %1: ").arg(point) + QString::fromLocal8Bit(game.readAll());
    }
    const QString output = QString::fromLocal8Bit(game.readAll());
    if (game.exitStatus() != QProcess::CrashExit) {
        return QStringLiteral("exited with %1 instead of crashing at %2: ").arg(game.exitCode()).arg(point) + output;
    }
    const QStringList lines = output.split(QLatin1Char('\n')).filter(QStringLiteral("UPSCALE_TEST_CRASH "));
    if (lines.isEmpty()) {
        return QStringLiteral("crashed elsewhere: ") + output;
    }
    // Its own output can share the line: glmark2 names a scene before its frames.
    return lines.last().mid(lines.last().indexOf(QStringLiteral("UPSCALE_TEST_CRASH ")));
}

// The effect's per-window and per-program records, from the line the test
// driver adds to the support information.
inline QString crashRecords(const QString &status)
{
    const QStringList lines = status.split(QLatin1Char('\n')).filter(QStringLiteral("records: "));
    return lines.isEmpty() ? status : lines.first().trimmed();
}
