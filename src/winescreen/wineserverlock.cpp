/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineserverlock.h"

#include <QFile>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

std::optional<WinePrefixIdentity> winePrefixIdentity(const QString &prefixDirectory)
{
    struct stat status = {};
    if (::stat(QFile::encodeName(prefixDirectory).constData(), &status) != 0 || !S_ISDIR(status.st_mode)) {
        return std::nullopt;
    }
    return WinePrefixIdentity{.device = status.st_dev, .inode = status.st_ino};
}

QString wineServerLockPath(const QString &temporaryDirectory, uid_t user, const WinePrefixIdentity &prefix)
{
    // The same names Wine prints with "/tmp/.wine-%u/server-%llx-%llx".
    return QStringLiteral("%1/.wine-%2/server-%3-%4/lock")
        .arg(temporaryDirectory)
        .arg(static_cast<qulonglong>(user))
        .arg(static_cast<qulonglong>(prefix.device), 0, 16)
        .arg(static_cast<qulonglong>(prefix.inode), 0, 16);
}

namespace
{

// The lock another process holds on the first byte, as F_GETLK reports it
// without taking one; l_type is F_UNLCK when nobody holds it.
std::optional<struct flock> testLock(const QString &lockPath, int &error)
{
    const int descriptor = ::open(QFile::encodeName(lockPath).constData(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        error = errno;
        return std::nullopt;
    }
    struct flock lock = {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 1;
    const bool tested = ::fcntl(descriptor, F_GETLK, &lock) == 0;
    error = errno;
    ::close(descriptor);
    if (!tested) {
        return std::nullopt;
    }
    return lock;
}

} // namespace

WineServerState wineServerState(const QString &lockPath)
{
    int error = 0;
    const std::optional<struct flock> lock = testLock(lockPath, error);
    if (!lock) {
        // Without a lock file no server has run for this prefix since the
        // temporary directory was last cleaned.
        return error == ENOENT ? WineServerState::Stopped : WineServerState::Unknown;
    }
    return lock->l_type == F_UNLCK ? WineServerState::Stopped : WineServerState::Running;
}

std::optional<pid_t> wineServerProcess(const QString &lockPath)
{
    int error = 0;
    const std::optional<struct flock> lock = testLock(lockPath, error);
    // A holder in a process namespace this one cannot see is reported as 0.
    if (!lock || lock->l_type == F_UNLCK || lock->l_pid <= 0) {
        return std::nullopt;
    }
    return lock->l_pid;
}

bool wineProcessExists(pid_t process)
{
    // Signal 0 only checks; EPERM means it exists but belongs to someone else.
    return ::kill(process, 0) == 0 || errno == EPERM;
}
