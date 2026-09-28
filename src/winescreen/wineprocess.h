/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <optional>

#include <sys/types.h>

/*
 * What the companion needs to know about a running Wine process: whose it is,
 * how it sees the file system, its environment, and how it was started.
 *
 * Neither Qt nor POSIX can read another process's environment, so gathering
 * these is platform-specific: wineprocess_proc.cpp reads Linux's /proc, and
 * wineprocess_unsupported.cpp answers nothing elsewhere, which leaves the game
 * untouched.
 */
struct WineProcess
{
    uid_t owner = 0;
    // The process's own view of the file system. A path the process uses is
    // reached by prefixing it with this, which also reaches into a container
    // such as the Steam runtime, Flatpak or Snap.
    QString root;
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
};

std::optional<WineProcess> wineProcess(qint64 pid);
