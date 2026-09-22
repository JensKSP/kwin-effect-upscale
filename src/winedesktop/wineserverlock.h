/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

#include <optional>

#include <sys/types.h>

/*
 * Whether a Wine server is running for a prefix.
 *
 * Every Wine server holds a write lock on the first byte of
 * <tmp>/.wine-<uid>/server-<device>-<inode>/lock for as long as it runs, where
 * the numbers are the prefix directory's device and inode in hexadecimal
 * (Wine server/request.c, create_server_dir and create_server_lock). While it
 * runs it rewrites the prefix's registry files, so they may only be edited while
 * no server holds that lock.
 *
 * The lock is only ever tested, never taken: a server that finds its lock taken
 * gives up and exits.
 */

enum class WineServerState {
    Stopped,
    Running,
    // The lock could not be tested; treat it as running.
    Unknown,
};

struct WinePrefixIdentity
{
    dev_t device = 0;
    ino_t inode = 0;

    bool operator==(const WinePrefixIdentity &other) const = default;
};

/*
 * The device and inode of a prefix directory, which are what names its server's
 * lock. Nothing when the path is not a directory.
 */
std::optional<WinePrefixIdentity> winePrefixIdentity(const QString &prefixDirectory);

/*
 * The lock file of the server for the prefix with that identity, below the given
 * temporary directory as the game sees it, for the given user.
 */
QString wineServerLockPath(const QString &temporaryDirectory, uid_t user, const WinePrefixIdentity &prefix);

WineServerState wineServerState(const QString &lockPath);

/*
 * The process that holds the lock, as this process numbers it. A server keeps
 * running for a few seconds after the game's last process has exited, and after
 * the game has gone its view of the file system, through which the lock was
 * reached, may have gone with it; waiting for this process is what stays valid.
 */
std::optional<pid_t> wineServerProcess(const QString &lockPath);

/*
 * Whether a process with that number still exists.
 */
bool wineProcessExists(pid_t process);
