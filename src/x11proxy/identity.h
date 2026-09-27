// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QStringList>
namespace UpscaleX11
{
/*
 * What a connecting process runs, as far as the proxy can tell before a single
 * X11 byte is read.
 *
 * A native program runs itself, and `program` is its own path. Wine runs a
 * Windows program and names it, as an absolute path, in the command line of
 * the process running it, so `program` is that path instead. Wine's own
 * components name themselves below the Windows system directory; `component`
 * marks them, because they belong to the prefix rather than to any game and
 * identify nothing a profile would be written for.
 *
 * A program that a runtime runs rather than the system is named with a scheme,
 * so that one pattern can single out a runtime's programs and no pattern can
 * confuse one with a program of the host. A native program carries no scheme,
 * so patterns written for one keep working. Separators are normalized to `/`,
 * because a pattern is a regular expression and a backslash in it would have
 * to be written twice.
 *
 * The name says where a program runs as well as what it is, in the shape of a
 * URI: `<scheme>://<where>/<program>`. Wine's prefix is an absolute path, so
 * it occupies the path and leaves the authority empty the way `file:///` does:
 *
 *     wine:///home/me/.steam/.../compatdata/228380/pfx/Z:/.../Wreckfest.exe
 *
 * A container's identifier is a token instead, so it occupies the authority:
 * `docker://<container>/usr/bin/game`. Those schemes are left open; only Wine
 * is resolved so far. One pattern can therefore single out every program of a
 * runtime, one program wherever it is installed, or one program in one place.
 *
 * The names are not percent-encoded, although they are otherwise URIs. Game
 * paths are full of spaces, and encoding them would mean writing a pattern
 * against `Rocket%20League`, which is not the name anybody reads on disk.
 *
 * Neither Qt nor POSIX can read another process's command line or environment,
 * so gathering this is platform-specific: identity_proc.cpp reads Linux's
 * /proc, identity_unsupported.cpp answers nothing elsewhere, and a connection
 * whose identity is unknown is forwarded untouched.
 */
struct ProgramIdentity
{
    /** The Unix executable, which for Wine is the loader every game shares. */
    QString executable;
    /** The program being run, with `/` separators and no scheme. */
    QString program;
    /** WINEPREFIX, or Wine's default below HOME, for a Wine process only. */
    QString prefix;
    /** The program is one of Wine's own, not the prefix's. */
    bool component = false;

    bool isWine() const
    {
        return !prefix.isEmpty();
    }
    /** Every string a profile may be matched against, in match order. */
    QStringList candidates() const;
};
/** What @p pid runs, or an empty identity where this cannot be read. */
ProgramIdentity upscaleProgramIdentity(quint32 pid);
/** Whether @p program is Wine naming a Windows program on a lettered drive. */
bool upscaleWindowsPath(const QString &program);
/**
 * The Windows program of @p prefix that is not one of Wine's own, or an empty
 * string while only Wine's components are running.
 *
 * A prefix is one Wine server, one registry and one Windows desktop, so its
 * programs share one screen. Wine's components start before the game and name
 * only themselves, which is why the game has to be found beside them rather
 * than asked of the connecting process.
 */
QString upscalePrefixProgram(const QString &prefix);
/** Whether @p program is one of Wine's own, below its Windows directory. */
bool upscaleWineComponent(const QString &program);
/** @p program with `/` separators, as a pattern is written against it. */
QString upscaleProgramPath(const QString &program);
/** `<scheme>://<where>/<program>`, or empty if either part is missing. */
QString upscaleRuntimeIdentity(QStringView scheme, const QString &where, const QString &program);
}
