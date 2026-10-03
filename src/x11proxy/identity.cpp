// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
#include <QDir>
#include <QFileInfo>
namespace UpscaleX11
{
QString upscaleWithoutLongPathPrefix(const QString &path)
{
    // Proton starts its own helpers so, xalia among them, and a program past
    // Windows' old path limit may be started so too.
    for (const QLatin1String prefix : {QLatin1String(R"(\\?\)"), QLatin1String(R"(\??\)")}) {
        if (path.startsWith(prefix)) {
            return path.sliced(prefix.size());
        }
    }
    return path;
}

bool upscaleWineLoader(const QString &path)
{
    // By name, as wherever Wine is installed: Debian's /usr/lib/wine, a
    // Proton build's files/bin, or a build tree.
    const QString name = QFileInfo(path).fileName();
    return name == QLatin1String("wine") || name == QLatin1String("wine64") || name == QLatin1String("wine-preloader")
        || name == QLatin1String("wine64-preloader");
}

bool upscaleWindowsPath(const QString &program)
{
    // Wine names a Windows program by an absolute path on a lettered drive.
    // The separator is a backslash, which a Unix program path never carries in
    // this position, so the two forms cannot be confused for one another.
    return program.size() > 3 && program.at(0).isLetter() && program.at(1) == QLatin1Char(':')
        && program.at(2) == QLatin1Char('\\');
}

QString upscaleWindowsPathFor(const QString &unixPath, const QList<WineDrive> &drives)
{
    const WineDrive *closest = nullptr;
    QString closestRoot;
    for (const WineDrive &drive : drives) {
        const QString root = drive.root.endsWith(QLatin1Char('/')) ? drive.root : drive.root + QLatin1Char('/');
        if (unixPath.startsWith(root) && (!closest || root.size() > closestRoot.size())) {
            closest = &drive;
            closestRoot = root;
        }
    }
    if (!closest) {
        return {};
    }
    QString rest = unixPath.mid(closestRoot.size());
    rest.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return closest->letter.toUpper() + QStringLiteral(":\\") + rest;
}

QString upscaleProgramPath(const QString &program)
{
    QString path = program;
    // A pattern is a regular expression, in which a backslash would have to be
    // written twice. The case Wine reports is the case on disk, the name a
    // person reads and writes a pattern against, so it is kept, and so is the
    // drive letter: which Unix path a letter stands for is a property of that
    // prefix and of no other.
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // A path may step back out of a folder, as Proton's xalia is started from
    // share/wine/../xalia; the program is named where it is.
    return QDir::cleanPath(path);
}

bool upscaleWineComponent(const QString &program)
{
    // Wine keeps its own components on the prefix's own drive, below its
    // Windows directory. That location is the whole test, so no list of
    // component names is needed and one Wine adds later is recognized without
    // a change here. Windows paths are case-insensitive and Wine does not
    // spell this one consistently, so the test is made on a folded copy.
    const QString folded = program.toLower();
    return folded.startsWith(QStringLiteral("c:/windows/system32/"))
        || folded.startsWith(QStringLiteral("c:/windows/syswow64/"));
}

QString upscaleRuntimeIdentity(QStringView scheme, const QString &where, const QString &program)
{
    if (where.isEmpty() || program.isEmpty()) {
        return {};
    }
    // Wine's program names a drive and brings no leading separator; a path
    // inside a container already has one, and must not be given a second.
    const QLatin1String separator(program.startsWith(QLatin1Char('/')) ? "" : "/");
    return scheme + QStringLiteral("://") + where + separator + program;
}

QString upscaleFlatpakApplication(const QByteArray &info)
{
    // A key file, whose [Application] group names the application by its ID;
    // a runtime run on its own is described by [Runtime] and names none.
    bool application = false;
    const QList<QByteArray> lines = info.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray entry = line.trimmed();
        if (entry.startsWith('[')) {
            application = entry == "[Application]";
        } else if (application && entry.startsWith("name=")) {
            return QString::fromUtf8(entry.sliced(5)).trimmed();
        }
    }
    return {};
}

QStringList ProgramIdentity::candidates() const
{
    // A program a runtime runs is named for where it runs as well as for what
    // it is; a program of the host is named by its path alone. The runtime's
    // name comes first, because it is the identity a profile is written for,
    // while the executable behind it is the loader every such program shares
    // and so tells them apart from nothing.
    QStringList strings;
    QString named = program;
    if (isWine()) {
        named = upscaleRuntimeIdentity(u"wine", prefix, program);
    } else if (!flatpak.isEmpty() && program.startsWith(QLatin1String("/app/"))) {
        // As the effect names it: a program of the runtime, outside /app,
        // keeps its path, and an exact pattern for it matches either way.
        named = upscaleRuntimeIdentity(u"flatpak", flatpak, program);
    }
    for (const QString &candidate : {named, executable}) {
        if (!candidate.isEmpty() && !strings.contains(candidate)) {
            strings.append(candidate);
        }
    }
    return strings;
}
}
