/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wineserverlock.h"

#include <QSize>
#include <QString>

#include <optional>

/*
 * Setting and removing the virtual desktop in a prefix whose game has exited.
 *
 * The prefix was located and proven while the game ran. Afterwards the game's
 * view of the file system may be gone, so the file is reached by the path the
 * game used, and only if that path still leads to the very directory that was
 * proven: same device, same inode. Where a container gave the game other paths
 * than the host has, that check fails and nothing is written.
 */

struct WineDesktopTarget
{
    // The prefix by the path the game used, and the identity proven for it.
    QString prefix;
    WinePrefixIdentity identity;
    // Proton's compatibility data directory, whose pfx.lock Proton holds while
    // it prepares the prefix; empty outside Proton.
    QString steamCompatData;
    // The host's temporary directory, where a server started since would hold
    // its lock if its /tmp is the host's; checked again after writing.
    QString temporaryDirectory;
};

enum class WineWriteResult {
    Written,
    // A Wine server holds the prefix, or Proton is preparing it: try again
    // after it has finished.
    Busy,
    // The path no longer leads to the proven prefix, or its user.reg is not
    // one to replace.
    Unreachable,
    // The prefix has a virtual desktop the user set; it is left alone.
    DesktopOfTheUser,
    WriteFailed,
};

/*
 * Gives the prefix a virtual desktop of that size. `ours` is the size this
 * companion set before, if it did: a desktop of exactly that size is replaced,
 * any other one is the user's.
 */
WineWriteResult wineSetDesktop(const WineDesktopTarget &target, uid_t user, const QSize &size, const std::optional<QSize> &ours, qint64 modifiedSeconds);

/*
 * Removes the desktop this companion set, `ours`. A desktop the user changed
 * since is left as it is.
 */
WineWriteResult wineClearDesktop(const WineDesktopTarget &target, uid_t user, const QSize &ours);
