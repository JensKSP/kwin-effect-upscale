/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

// All games acts only for a program recognized as a game, and a test's own
// client is none. A desktop entry in the Game category that starts it makes it
// one, as installing a game does. The entry goes to the applications directory
// of the session's own data, which tools/run-integration-test.py keeps
// private, or of QStandardPaths' test mode where KWin runs inside the test.
// With @p game false it is removed again, and the program is what any desktop
// program is to All games.
inline bool upscaleDeclareGame(const QString &program, bool game = true)
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString path = directory + QStringLiteral("/upscale-test-") + QFileInfo(program).fileName() + QStringLiteral(".desktop");
    if (!game) {
        return !QFile::exists(path) || QFile::remove(path);
    }
    QFile entry(path);
    if (!QDir().mkpath(directory) || !entry.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray contents = QByteArrayLiteral("[Desktop Entry]\nType=Application\nName=Upscale test game\nCategories=Game;\nExec=\"")
        + QFileInfo(program).canonicalFilePath().toUtf8() + QByteArrayLiteral("\"\n");
    return entry.write(contents) == contents.size();
}
