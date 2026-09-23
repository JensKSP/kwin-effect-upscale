/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wineregistry.h"
#include "wineserverlock.h"

#include <QSize>
#include <QString>

#include <memory>
#include <optional>

/*
 * Describing and undescribing the screen in a prefix whose game has exited.
 *
 * The prefix was located and proven while the game ran, and is reached through
 * the directory held open since then. Without that, it is reached by the path
 * the game used, and only if that path still leads to the very directory that
 * was proven: same device, same inode.
 */

/*
 * A prefix directory held open. Opened while the game runs, through the game's
 * own view of the file system, it keeps reaching the very directory that was
 * proven: after the game and its view have gone, and where a container gave
 * the game other paths than the host has. Everything is read and written
 * relative to it.
 */
class WineDirectory
{
public:
    // Nothing unless the path leads to a directory with that identity.
    static std::shared_ptr<WineDirectory> open(const QString &path, const WinePrefixIdentity &identity);
    ~WineDirectory();

    WineDirectory(const WineDirectory &) = delete;
    WineDirectory &operator=(const WineDirectory &) = delete;

    int descriptor() const;
    // Whether it is still that directory and has not been removed since.
    bool isStill(const WinePrefixIdentity &identity) const;

private:
    explicit WineDirectory(int descriptor);

    int m_descriptor;
};

struct WineScreenTarget
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
    // The directory held open since it was proven, when there is one; without
    // it the path is opened and checked against the identity instead.
    std::shared_ptr<WineDirectory> directory;
};

enum class WineWriteResult {
    Written,
    // The file was replaced, but a Wine server started in the meantime and may
    // write its old copy back when it exits: the caller records the change and
    // writes it again after that run.
    WrittenMeanwhile,
    // A Wine server holds the prefix, or Proton is preparing it: try again
    // after it has finished.
    Busy,
    // The path no longer leads to the proven prefix, its registry is not one to
    // replace, or the prefix has not described its devices yet.
    Unreachable,
    // The prefix runs its programs in a virtual desktop the user set; it is left
    // alone.
    DesktopOfTheUser,
    WriteFailed,
};

/*
 * Tells the prefix it has those screens, the first of them the one the prepared
 * program is on, in place of anything this companion described before. A prefix
 * whose programs run in a virtual desktop of the user's is left alone.
 */
WineWriteResult wineSetScreens(const WineScreenTarget &target, uid_t user, const QList<WineScreen> &screens, qint64 modifiedSeconds);

/*
 * The screens the prefix describes now, empty when it describes none or cannot
 * be read.
 */
QList<WineScreen> wineScreensIn(const WineDirectory &directory);

/*
 * Takes the description away again, so that the prefix asks the display server
 * for its screen as it did before.
 */
WineWriteResult wineClearScreen(const WineScreenTarget &target, uid_t user);
