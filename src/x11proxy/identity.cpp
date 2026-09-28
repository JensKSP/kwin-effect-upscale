// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
namespace UpscaleX11
{
bool upscaleWindowsPath(const QString &program)
{
    // Wine names a Windows program by an absolute path on a lettered drive.
    // The separator is a backslash, which a Unix program path never carries in
    // this position, so the two forms cannot be confused for one another.
    return program.size() > 3 && program.at(0).isLetter() && program.at(1) == QLatin1Char(':')
        && program.at(2) == QLatin1Char('\\');
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
    return path;
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

QStringList ProgramIdentity::candidates() const
{
    // A program a runtime runs is named for where it runs as well as for what
    // it is; a program of the host is named by its path alone. The runtime's
    // name comes first, because it is the identity a profile is written for,
    // while the executable behind it is the loader every such program shares
    // and so tells them apart from nothing.
    QStringList strings;
    const QString named = isWine() ? upscaleRuntimeIdentity(u"wine", prefix, program) : program;
    for (const QString &candidate : {named, executable}) {
        if (!candidate.isEmpty() && !strings.contains(candidate)) {
            strings.append(candidate);
        }
    }
    return strings;
}
}
