// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
namespace UpscaleX11
{
namespace
{
// The NUL-separated lists /proc gives for a command line and an environment.
QList<QByteArray> readList(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray contents = file.readAll();
    if (contents.endsWith('\0')) {
        contents.chop(1);
    }
    if (contents.isEmpty()) {
        return {};
    }
    return contents.split('\0');
}

QString environmentValue(const QList<QByteArray> &environment, QByteArrayView name)
{
    for (const QByteArray &entry : environment) {
        if (entry.size() > name.size() && entry.startsWith(name) && entry.at(name.size()) == '=') {
            return QString::fromLocal8Bit(entry.mid(int(name.size()) + 1));
        }
    }
    return {};
}

// The prefix as the process names it: WINEPREFIX, or Wine's own default below
// HOME, which is where Wine looks when the variable is unset.
QString prefixOf(const QList<QByteArray> &environment)
{
    const QString prefix = environmentValue(environment, "WINEPREFIX");
    if (!prefix.isEmpty()) {
        return QDir::cleanPath(prefix);
    }
    const QString home = environmentValue(environment, "HOME");
    return home.isEmpty() ? QString() : QDir::cleanPath(home + QStringLiteral("/.wine"));
}
} // namespace

ProgramIdentity upscaleProgramIdentity(quint32 pid)
{
    const QString directory = QStringLiteral("/proc/%1").arg(pid);
    ProgramIdentity identity;
    identity.executable = QFileInfo(directory + QStringLiteral("/exe")).symLinkTarget();
    const QList<QByteArray> command = readList(directory + QStringLiteral("/cmdline"));
    if (command.isEmpty()) {
        return identity;
    }
    const QString first = QString::fromLocal8Bit(command.constFirst());
    if (!upscaleWindowsPath(first)) {
        identity.program = identity.executable;
        return identity;
    }
    // A Windows path in the command line is Wine running that program. The
    // prefix decides the screen, and it is read from the environment rather
    // than from the path, which may sit on any drive the prefix maps.
    identity.program = upscaleProgramPath(first);
    identity.component = upscaleWineComponent(identity.program);
    identity.prefix = prefixOf(readList(directory + QStringLiteral("/environ")));
    return identity;
}

QString upscalePrefixProgram(const QString &prefix)
{
    if (prefix.isEmpty()) {
        return {};
    }
    const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool numeric = false;
        const uint pid = entry.toUInt(&numeric);
        // Only this user's processes can be read, and the game runs as this
        // user; anything else stays invisible here and matches nothing.
        if (!numeric) {
            continue;
        }
        const ProgramIdentity identity = upscaleProgramIdentity(pid);
        if (identity.prefix == prefix && !identity.component && !identity.program.isEmpty()) {
            return identity.program;
        }
    }
    return {};
}
}
