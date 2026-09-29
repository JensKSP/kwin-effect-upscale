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
// A path as Wine sees it: with its links resolved, as far as it exists.
QString resolved(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists()) {
        return info.canonicalFilePath();
    }
    const QString directory = QFileInfo(info.absolutePath()).canonicalFilePath();
    return directory.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : directory + QLatin1Char('/') + info.fileName();
}

// The drives of a prefix, which are the links below its dosdevices.
QList<WineDrive> drivesOf(const QString &prefix)
{
    QList<WineDrive> drives;
    const QDir devices(prefix + QStringLiteral("/dosdevices"));
    const QStringList entries = devices.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QString root = QFileInfo(devices.filePath(entry)).canonicalFilePath();
        if (entry.size() == 2 && entry.at(0).isLetter() && entry.at(1) == QLatin1Char(':') && !root.isEmpty()) {
            drives.append({entry.at(0), root});
        }
    }
    return drives;
}
} // namespace

ProgramIdentity upscaleProgramIdentity(quint32 pid)
{
    const QString directory = QStringLiteral("/proc/%1").arg(pid);
    ProgramIdentity identity;
    identity.executable = QFileInfo(directory + QStringLiteral("/exe")).symLinkTarget();
    // A program Flatpak runs has the sandbox's description at the root it
    // sees, which /proc shows through that root; a program of the host has
    // none there. Its path is the one inside the sandbox, below /app.
    QFile info(directory + QStringLiteral("/root/.flatpak-info"));
    if (info.open(QIODevice::ReadOnly)) {
        identity.flatpak = upscaleFlatpakApplication(info.read(qint64(64) * 1024));
    }
    const QList<QByteArray> command = readList(directory + QStringLiteral("/cmdline"));
    if (command.isEmpty()) {
        return identity;
    }
    QString first = QString::fromLocal8Bit(command.constFirst());
    QString prefix;
    // Wine started with a program's Unix path keeps that path in the command
    // line, where the process runs Wine's loader rather than the program:
    // Wine names the program on the drive that holds it, and so does this.
    // Found 2026-09-29 with `wine /path/to/game.exe`, which the proxy did not
    // identify, holding its prefix's connections for their ten seconds.
    if (!upscaleWindowsPath(first) && first.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        const QString program = resolved(QDir(directory + QStringLiteral("/cwd")).absoluteFilePath(first));
        if (program != resolved(identity.executable)) {
            prefix = prefixOf(readList(directory + QStringLiteral("/environ")));
            first = upscaleWindowsPathFor(program, drivesOf(prefix));
        }
    }
    if (!upscaleWindowsPath(first)) {
        identity.program = identity.executable;
        return identity;
    }
    // A Windows path in the command line is Wine running that program. The
    // prefix decides the screen, and it is read from the environment rather
    // than from the path, which may sit on any drive the prefix maps.
    identity.program = upscaleProgramPath(first);
    identity.component = upscaleWineComponent(identity.program);
    identity.prefix = prefix.isEmpty() ? prefixOf(readList(directory + QStringLiteral("/environ"))) : prefix;
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
