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

// The prefix describes its screen in the machine's registry and the user's
// virtual desktop in the user's own.
const char machineRegistry[] = "system.reg";
const char userRegistry[] = "user.reg";

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
bool plainRegistry(int directory, const char *name, uid_t user)
{
    struct stat status = {};
    return ::fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1
        && status.st_uid == user;
}

std::optional<QByteArray> readRegistry(int directory, const char *name)
{
    const int descriptor = ::openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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

// Replaces a registry file in one step: a new file beside it with the
// permissions the old one had, then a rename over it.
bool replaceRegistry(int directory, const char *name, const QByteArray &text)
{
    struct stat status = {};
    if (::fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
        return false;
    }
    const QByteArray temporary = QByteArray(name) + ".upscale-" + QUuid::createUuid().toByteArray(QUuid::Id128);
    const mode_t mode = status.st_mode & 07777;
    const int descriptor = ::openat(directory, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
    if (descriptor < 0) {
        return false;
    }
    // The mode given to openat() passes through the umask; this one does not.
    const bool written = ::fchmod(descriptor, mode) == 0 && writeAll(descriptor, text);
    if (::close(descriptor) != 0 || !written || ::renameat(directory, temporary.constData(), directory, name) != 0) {
        ::unlinkat(directory, temporary.constData(), 0);
        return false;
    }
    return true;
}

// The two values that would give the prefix a virtual desktop, from the user's
// own registry.
WineDesktopValues desktopValuesIn(const WineDirectory &directory)
{
    const std::optional<QByteArray> contents = readRegistry(directory.descriptor(), userRegistry);
    return contents && wineIsRegistry(*contents) ? wineDesktopValues(*contents) : WineDesktopValues{};
}

// The registry's text, once it is certain that it may be replaced now. A prefix
// whose programs run in a virtual desktop of the user's is not ours to describe.
WineWriteResult read(const WineDesktopTarget &target, uid_t user, const std::shared_ptr<WineDirectory> &directory, QByteArray &text)
{
    if (!directory || !plainRegistry(directory->descriptor(), machineRegistry, user)) {
        return WineWriteResult::Unreachable;
    }
    if (serverRunning(target, user)) {
        return WineWriteResult::Busy;
    }
    const WineDesktopValues desktop = desktopValuesIn(*directory);
    if (desktop.desktop || desktop.defaultSize) {
        return WineWriteResult::DesktopOfTheUser;
    }
    const std::optional<QByteArray> contents = readRegistry(directory->descriptor(), machineRegistry);
    if (!contents || !wineIsRegistry(*contents)) {
        return WineWriteResult::Unreachable;
    }
    text = *contents;
    return WineWriteResult::Written;
}

WineWriteResult replace(const WineDesktopTarget &target, uid_t user, const std::shared_ptr<WineDirectory> &directory, const QByteArray &text)
{
    if (!replaceRegistry(directory->descriptor(), machineRegistry, text)) {
        return WineWriteResult::WriteFailed;
    }
    // A server that started between the check and the rename may have read
    // the old file and will write it back when it exits; the caller records the
    // change and writes it again after that run.
    return serverRunning(target, user) ? WineWriteResult::WrittenMeanwhile : WineWriteResult::Written;
}

} // namespace

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

QList<WineScreen> wineScreensIn(const WineDirectory &directory)
{
    const std::optional<QByteArray> contents = readRegistry(directory.descriptor(), machineRegistry);
    return contents && wineIsRegistry(*contents) ? wineScreens(*contents) : QList<WineScreen>{};
}

WineWriteResult wineSetScreens(const WineDesktopTarget &target, uid_t user, const QList<WineScreen> &screens, qint64 modifiedSeconds)
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
    // Without the devices the prefix described for itself there is nothing to
    // describe a screen for; it has them once a program of its own has run.
    const std::optional<QByteArray> changed = wineWithScreens(text, screens, modifiedSeconds);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, directory, *changed);
}

WineWriteResult wineClearScreen(const WineDesktopTarget &target, uid_t user)
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
    if (wineScreens(text).isEmpty()) {
        return WineWriteResult::Written;
    }
    const std::optional<QByteArray> changed = wineWithoutScreen(text);
    if (!changed) {
        return WineWriteResult::Unreachable;
    }
    return replace(target, user, directory, *changed);
}
