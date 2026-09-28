/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QFile>
#include <QString>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Holds a lock in a child process, as a Wine server holds its lock (a write
 * lock on the first byte) or Proton its pfx.lock (flock). It has to be another
 * process: F_GETLK never reports a lock of the process that asks, and a flock
 * taken again by its own holder succeeds.
 */
class LockHolder
{
public:
    enum class Kind {
        WineServer,
        Proton,
    };

    explicit LockHolder(const QString &path, Kind kind = Kind::WineServer)
    {
        const QByteArray name = QFile::encodeName(path);
        int ready[2];
        int release[2];
        if (::pipe(ready) != 0 || ::pipe(release) != 0) {
            return;
        }
        m_child = ::fork();
        if (m_child == 0) {
            // The child keeps only its own ends, so that it sees the parent
            // go even if the parent never releases it.
            ::close(ready[0]);
            ::close(release[1]);
            holdUntilReleased(name.constData(), kind, ready[1], release[0]);
        }
        ::close(ready[1]);
        ::close(release[0]);
        m_release = release[1];
        char locked = 0;
        m_locked = ::read(ready[0], &locked, 1) == 1 && locked == 1;
        ::close(ready[0]);
    }

    ~LockHolder()
    {
        if (m_child > 0) {
            ::close(m_release);
            ::waitpid(m_child, nullptr, 0);
        }
    }

    LockHolder(const LockHolder &) = delete;
    LockHolder &operator=(const LockHolder &) = delete;

    bool locked() const
    {
        return m_locked;
    }

    pid_t process() const
    {
        return m_child;
    }

private:
    [[noreturn]] static void holdUntilReleased(const char *path, Kind kind, int ready, int release)
    {
        // A test that hangs is worse than one that fails.
        ::alarm(120);
        const int descriptor = ::open(path, O_RDWR | O_CREAT, 0600);
        bool locked = false;
        if (descriptor >= 0 && kind == Kind::Proton) {
            locked = ::flock(descriptor, LOCK_EX | LOCK_NB) == 0;
        } else if (descriptor >= 0) {
            struct flock lock = {};
            lock.l_type = F_WRLCK;
            lock.l_whence = SEEK_SET;
            lock.l_start = 0;
            lock.l_len = 1;
            locked = ::fcntl(descriptor, F_SETLK, &lock) == 0;
        }
        const char answer = locked ? 1 : 0;
        char released = 0;
        if (::write(ready, &answer, 1) != 1 || ::read(release, &released, 1) < 0) {
            ::_exit(1);
        }
        ::_exit(0);
    }

    pid_t m_child = -1;
    int m_release = -1;
    bool m_locked = false;
};
