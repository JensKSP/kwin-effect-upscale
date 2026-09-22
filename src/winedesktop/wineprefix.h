/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wineprocess.h"
#include "wineserverlock.h"

#include <expected>

/*
 * The prefix a running Wine game uses, proven rather than guessed.
 *
 * The path comes from the game's own environment, WINEPREFIX or Wine's default
 * $HOME/.wine, and is reached through the game's view of the file system. What
 * proves it is the lock: the Wine server running for the game holds a lock
 * named after the prefix directory's device and inode, so a path that leads
 * anywhere else finds no held lock and is refused. That holds for every Wine
 * and Proton flavour. Proton's own variables are checked in addition where they
 * are set.
 */

enum class WinePrefixRefusal {
    // The platform or the permissions do not let the companion see the process.
    ProcessUnreadable,
    OtherUser,
    NoPrefix,
    NotADirectory,
    NotOwned,
    RegistryMissing,
    // user.reg is a link or not a regular file, so replacing it would not
    // replace what Wine reads.
    RegistryNotPlain,
    NotARegistry,
    // Proton's variables and the window disagree about which game this is.
    SteamMismatch,
    // No server holds this prefix's lock, so it is not the prefix the game uses.
    ServerNotRunning,
};

struct WinePrefix
{
    // The prefix as the companion reaches it, and as the game names it.
    QString path;
    QString gamePath;
    // The game's temporary directory, where its server's lock lives.
    QString temporaryDirectory;
    WinePrefixIdentity identity;
    // Proton's compatibility data directory as the companion reaches it, and
    // the Steam application; both empty outside Proton.
    QString steamCompatDataPath;
    QString steamAppId;
};

/*
 * The prefix of a running Wine process owned by the given user. The window
 * class, when it is Proton's steam_app_<id>, has to name the same game as the
 * process's environment.
 */
std::expected<WinePrefix, WinePrefixRefusal> wineLocatePrefix(const WineProcess &process, uid_t user, const QString &windowClass);

QString wineRegistryPath(const WinePrefix &prefix);

/*
 * Whether the file at that path is a user.reg the companion may replace: a
 * plain file with a single name, owned by the user, in Wine's format.
 */
std::optional<WinePrefixRefusal> wineCheckRegistry(const QString &path, uid_t user);
