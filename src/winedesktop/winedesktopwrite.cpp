/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktopwrite.h"
#include "wineprefix.h"
#include "wineregistry.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace
{

QString sizeText(const QSize &size)
{
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

// Proton flock()s <compatdata>/pfx.lock while it prepares a prefix and waits
// for it without a time limit (its filelock.py). Holding it while writing makes
// a launch in the meantime wait instead of reading a half-changed prefix. It is
// never created: without the file, no Proton prepares this prefix.
class ProtonLock
{
public:
    explicit ProtonLock(const QString &compatData)
    {
        if (compatData.isEmpty()) {
            m_held = true;
            return;
        }
        m_descriptor = ::open(QFile::encodeName(compatData + QStringLiteral("/pfx.lock")).constData(), O_RDWR | O_CLOEXEC);
        if (m_descriptor < 0) {
            m_held = errno == ENOENT;
            return;
        }
        m_held = ::flock(m_descriptor, LOCK_EX | LOCK_NB) == 0;
    }

    ~ProtonLock()
    {
        if (m_descriptor >= 0) {
            ::close(m_descriptor);
        }
    }

    ProtonLock(const ProtonLock &) = delete;
    ProtonLock &operator=(const ProtonLock &) = delete;

    bool held() const
    {
        return m_held;
    }

private:
    int m_descriptor = -1;
    bool m_held = false;
};

QString registryPath(const WineDesktopTarget &target)
{
    return target.prefix + QStringLiteral("/user.reg");
}

bool serverRunning(const WineDesktopTarget &target, uid_t user)
{
    return wineServerState(wineServerLockPath(target.temporaryDirectory, user, target.identity)) != WineServerState::Stopped;
}

// The registry's text, once it is certain that it may be replaced now.
WineWriteResult read(const WineDesktopTarget &target, uid_t user, QByteArray &text)
{
    if (winePrefixIdentity(target.prefix) != target.identity || wineCheckRegistry(registryPath(target), user)) {
        return WineWriteResult::Unreachable;
    }
    if (serverRunning(target, user)) {
        return WineWriteResult::Busy;
    }
    QFile file(registryPath(target));
    if (!file.open(QIODevice::ReadOnly)) {
        return WineWriteResult::Unreachable;
    }
    text = file.readAll();
    return WineWriteResult::Written;
}

// Replaces the file in one step, through a temporary file and a rename, with
// the permissions it had.
WineWriteResult replace(const WineDesktopTarget &target, uid_t user, const QByteArray &text)
{
    const QString path = registryPath(target);
    const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(permissions) || file.write(text) != text.size() || !file.commit()) {
        return WineWriteResult::WriteFailed;
    }
    // A server that started between the check and the rename may have read
    // the old file and will write it back when it exits; the caller tries again
    // after that run.
    return serverRunning(target, user) ? WineWriteResult::Busy : WineWriteResult::Written;
}

bool isOurs(const WineDesktopValues &values, const QSize &ours)
{
    return values.desktop == QStringLiteral("Default") && values.defaultSize == sizeText(ours);
}

} // namespace

WineWriteResult wineSetDesktop(const WineDesktopTarget &target, uid_t user, const QSize &size, const std::optional<QSize> &ours, qint64 modifiedSeconds)
{
    const ProtonLock proton(target.steamCompatData);
    if (!proton.held()) {
        return WineWriteResult::Busy;
    }
    QByteArray text;
    if (const WineWriteResult result = read(target, user, text); result != WineWriteResult::Written) {
        return result;
    }
    // A value of either kind that this companion did not set is the user's,
    // including a size left behind when they switched their desktop off.
    const WineDesktopValues values = wineDesktopValues(text);
    const bool absent = !values.desktop && !values.defaultSize;
    if (!absent && !(ours && isOurs(values, *ours))) {
        return WineWriteResult::DesktopOfTheUser;
    }
    const std::optional<QByteArray> changed = wineWithVirtualDesktop(text, size, modifiedSeconds);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, *changed);
}

WineWriteResult wineClearDesktop(const WineDesktopTarget &target, uid_t user, const QSize &ours)
{
    const ProtonLock proton(target.steamCompatData);
    if (!proton.held()) {
        return WineWriteResult::Busy;
    }
    QByteArray text;
    if (const WineWriteResult result = read(target, user, text); result != WineWriteResult::Written) {
        return result;
    }
    const WineDesktopValues values = wineDesktopValues(text);
    if (!values.desktop && !values.defaultSize) {
        return WineWriteResult::Written;
    }
    if (!isOurs(values, ours)) {
        return WineWriteResult::DesktopOfTheUser;
    }
    const std::optional<QByteArray> changed = wineWithoutVirtualDesktop(text);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, *changed);
}
