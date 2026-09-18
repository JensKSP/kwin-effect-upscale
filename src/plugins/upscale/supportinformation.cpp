/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "supportinformation.h"

#include <QStringList>

namespace KWin
{

QString upscaleReportedStatus(const QString &information, QString *loadedBuild)
{
    if (loadedBuild) {
        loadedBuild->clear();
    }
    QStringList lines = information.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.constFirst().endsWith(QLatin1Char(':'))) {
        lines.removeFirst();
    }
    const QString buildPrefix = QStringLiteral("build: ");
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lines.at(index).startsWith(buildPrefix)) {
            const QString build = lines.takeAt(index).mid(buildPrefix.size()).trimmed();
            if (loadedBuild) {
                *loadedBuild = build;
            }
            break;
        }
    }
    const QString statusPrefix = QStringLiteral("status: ");
    if (!lines.isEmpty() && lines.constFirst().startsWith(statusPrefix)) {
        lines.replace(0, lines.constFirst().mid(statusPrefix.size()));
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

} // namespace KWin
