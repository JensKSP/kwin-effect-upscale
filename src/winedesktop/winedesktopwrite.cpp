/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktopwrite.h"
#include "wineprefix.h"
#include "wineregistry.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
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

const char registryName[] = "user.reg";

bool serverRunning(const WineDesktopTarget &target, uid_t user)
{
    return wineServerState(wineServerLockPath(target.temporaryDirectory, user, target.identity)) != WineServerState::Stopped;
}

// The directory to work in: the one held since the prefix was proven, or the
// path opened now, either only while it is still that directory.
std::shared_ptr<WineDirectory> directoryFor(const WineDesktopTarget &target)
{
    if (target.directory) {
        return target.directory->isStill(target.identity) ? target.directory : nullptr;
    }
    return WineDirectory::open(target.prefix, target.identity);
}

// A plain file with a single name, owned by the user: renaming a new file
// over it replaces what Wine reads.
bool plainRegistry(int directory, uid_t user)
{
    struct stat status = {};
    return ::fstatat(directory, registryName, &status, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1
        && status.st_uid == user;
}

std::optional<QByteArray> readRegistry(int directory)
{
    const int descriptor = ::openat(directory, registryName, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return std::nullopt;
    }
    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return std::nullopt;
    }
    return file.readAll();
}

bool writeAll(int descriptor, const QByteArray &text)
{
    qsizetype offset = 0;
    while (offset < text.size()) {
        const ssize_t count = ::write(descriptor, text.constData() + offset, static_cast<size_t>(text.size() - offset));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        offset += count;
    }
    return ::fsync(descriptor) == 0;
}

// Replaces user.reg in one step: a new file beside it with the permissions the
// old one had, then a rename over it.
bool replaceRegistry(int directory, const QByteArray &text)
{
    struct stat status = {};
    if (::fstatat(directory, registryName, &status, AT_SYMLINK_NOFOLLOW) != 0) {
        return false;
    }
    const QByteArray temporary = QByteArray(registryName) + ".upscale-" + QUuid::createUuid().toByteArray(QUuid::Id128);
    const mode_t mode = status.st_mode & 07777;
    const int descriptor = ::openat(directory, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
    if (descriptor < 0) {
        return false;
    }
    // The mode given to openat() passes through the umask; this one does not.
    const bool written = ::fchmod(descriptor, mode) == 0 && writeAll(descriptor, text);
    if (::close(descriptor) != 0 || !written || ::renameat(directory, temporary.constData(), directory, registryName) != 0) {
        ::unlinkat(directory, temporary.constData(), 0);
        return false;
    }
    return true;
}

// The registry's text, once it is certain that it may be replaced now.
WineWriteResult read(const WineDesktopTarget &target, uid_t user, const std::shared_ptr<WineDirectory> &directory, QByteArray &text)
{
    if (!directory || !plainRegistry(directory->descriptor(), user)) {
        return WineWriteResult::Unreachable;
    }
    if (serverRunning(target, user)) {
        return WineWriteResult::Busy;
    }
    const std::optional<QByteArray> contents = readRegistry(directory->descriptor());
    if (!contents || !wineIsRegistry(*contents)) {
        return WineWriteResult::Unreachable;
    }
    text = *contents;
    return WineWriteResult::Written;
}

WineWriteResult replace(const WineDesktopTarget &target, uid_t user, const std::shared_ptr<WineDirectory> &directory, const QByteArray &text)
{
    if (!replaceRegistry(directory->descriptor(), text)) {
        return WineWriteResult::WriteFailed;
    }
    // A server that started between the check and the rename may have read
    // the old file and will write it back when it exits; the caller tries again
    // after that run.
    return serverRunning(target, user) ? WineWriteResult::Busy : WineWriteResult::Written;
}

} // namespace

bool wineIsDesktop(const WineDesktopValues &values, const QSize &size)
{
    return values.desktop == QStringLiteral("Default") && values.defaultSize == sizeText(size);
}

std::shared_ptr<WineDirectory> WineDirectory::open(const QString &path, const WinePrefixIdentity &identity)
{
    const int descriptor = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        return nullptr;
    }
    const std::shared_ptr<WineDirectory> directory(new WineDirectory(descriptor));
    return directory->isStill(identity) ? directory : nullptr;
}

WineDirectory::WineDirectory(int descriptor)
    : m_descriptor(descriptor)
{
}

WineDirectory::~WineDirectory()
{
    ::close(m_descriptor);
}

int WineDirectory::descriptor() const
{
    return m_descriptor;
}

bool WineDirectory::isStill(const WinePrefixIdentity &identity) const
{
    struct stat status = {};
    // A removed directory keeps its identity while it is held, but has no
    // links left.
    return ::fstat(m_descriptor, &status) == 0 && status.st_nlink > 0 && status.st_dev == identity.device && status.st_ino == identity.inode;
}

WineDesktopValues wineDesktopValuesIn(const WineDirectory &directory)
{
    const std::optional<QByteArray> contents = readRegistry(directory.descriptor());
    return contents && wineIsRegistry(*contents) ? wineDesktopValues(*contents) : WineDesktopValues{};
}

WineWriteResult wineSetDesktop(const WineDesktopTarget &target, uid_t user, const QSize &size, const std::optional<QSize> &ours, qint64 modifiedSeconds)
{
    const ProtonLock proton(target.steamCompatData);
    if (!proton.held()) {
        return WineWriteResult::Busy;
    }
    const std::shared_ptr<WineDirectory> directory = directoryFor(target);
    QByteArray text;
    if (const WineWriteResult result = read(target, user, directory, text); result != WineWriteResult::Written) {
        return result;
    }
    // A value of either kind that this companion did not set is the user's,
    // including a size left behind when they switched their desktop off.
    const WineDesktopValues values = wineDesktopValues(text);
    const bool absent = !values.desktop && !values.defaultSize;
    if (!absent && !(ours && wineIsDesktop(values, *ours))) {
        return WineWriteResult::DesktopOfTheUser;
    }
    const std::optional<QByteArray> changed = wineWithVirtualDesktop(text, size, modifiedSeconds);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, directory, *changed);
}

WineWriteResult wineClearDesktop(const WineDesktopTarget &target, uid_t user, const QSize &ours)
{
    const ProtonLock proton(target.steamCompatData);
    if (!proton.held()) {
        return WineWriteResult::Busy;
    }
    const std::shared_ptr<WineDirectory> directory = directoryFor(target);
    QByteArray text;
    if (const WineWriteResult result = read(target, user, directory, text); result != WineWriteResult::Written) {
        return result;
    }
    const WineDesktopValues values = wineDesktopValues(text);
    if (!values.desktop && !values.defaultSize) {
        return WineWriteResult::Written;
    }
    if (!wineIsDesktop(values, ours)) {
        return WineWriteResult::DesktopOfTheUser;
    }
    const std::optional<QByteArray> changed = wineWithoutVirtualDesktop(text);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, directory, *changed);
}
