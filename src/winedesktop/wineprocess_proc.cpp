/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineprocess.h"

#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>

namespace
{

// The NUL-separated lists /proc gives for a command line and an environment.
std::optional<QList<QByteArray>> readList(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QByteArray contents = file.readAll();
    if (contents.endsWith('\0')) {
        contents.chop(1);
    }
    if (contents.isEmpty()) {
        return QList<QByteArray>{};
    }
    return contents.split('\0');
}

} // namespace

std::optional<WineProcess> wineProcess(qint64 pid)
{
    const QString directory = QStringLiteral("/proc/%1").arg(pid);
    struct stat status = {};
    if (::stat(QFile::encodeName(directory).constData(), &status) != 0) {
        return std::nullopt;
    }
    const std::optional<QList<QByteArray>> arguments = readList(directory + QStringLiteral("/cmdline"));
    const std::optional<QList<QByteArray>> variables = readList(directory + QStringLiteral("/environ"));
    if (!arguments || !variables) {
        return std::nullopt;
    }
    WineProcess process{
        .owner = status.st_uid,
        .root = directory + QStringLiteral("/root"),
        .executable = QFileInfo(directory + QStringLiteral("/exe")).symLinkTarget(),
        .arguments = {},
        .workingDirectory = QFileInfo(directory + QStringLiteral("/cwd")).symLinkTarget(),
        .environment = {},
    };
    for (const QByteArray &argument : *arguments) {
        process.arguments.append(QFile::decodeName(argument));
    }
    for (const QByteArray &variable : *variables) {
        const qsizetype separator = variable.indexOf('=');
        if (separator > 0) {
            process.environment.insert(QFile::decodeName(variable.first(separator)), QFile::decodeName(variable.mid(separator + 1)));
        }
    }
    return process;
}
