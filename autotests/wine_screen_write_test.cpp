/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lockholder.h"
#include "wineregistry.h"
#include "winescreenwrite.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace
{

const QByteArray s_user = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n"
    "\n"
    "#arch=win64\n"
    "\n"
    "[Control Panel\\\\Desktop] 1789805658\n"
    "\"ActiveWndTrackTimeout\"=dword:00000000\n");

// A prefix that has run once and kept what it found: a graphics card with an
// identifier of its own, and a monitor.
const QByteArray s_machine = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\\\Machine\n"
    "\n"
    "#arch=win64\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0000&0000] 1790154356\n"
    "\"DeviceDesc\"=\"Generic Non-PnP Monitor\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000\\\\Device Parameters] 1790154350\n"
    "\"VideoID\"=\"{65aaded5-ba18-41e2-9572-7290231b645a}\"\n");

const QSize s_size(2560, 1440);
const QList<WineScreen> s_screens = {{.rect = QRect(QPoint(), s_size), .rate = 60}};
const QList<WineScreen> s_smaller = {{.rect = QRect(0, 0, 1920, 1080), .rate = 60}};

} // namespace

class WineDesktopWriteTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void describesAndUndescribesTheScreen();
    void describesAnotherSizeOverItsOwn();
    void leavesAPrefixWithAVirtualDesktopAlone();
    void clearsPreparationAfterTheUserEnablesADesktop();
    void refusesAPrefixThatDescribesNoDevices();
    void waitsForARunningServer();
    void waitsForProton();
    void refusesADirectoryThatIsNoLongerTheProvenOne();
    void writesTheHeldDirectoryWhereverItsPathLeads();
    void refusesAHeldDirectoryThatWasRemoved();
    void keepsTheFilesPermissions();

private:
    QByteArray registry(const QString &name = QStringLiteral("system.reg")) const;
    void writeRegistry(const QByteArray &text, const QString &name = QStringLiteral("system.reg")) const;

    std::optional<QTemporaryDir> m_root;
    WineScreenTarget m_target;
};

void WineDesktopWriteTest::init()
{
    m_root.emplace();
    QVERIFY(m_root->isValid());
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("compatdata/228380/pfx"))));
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("tmp"))));
    m_target = {
        .prefix = m_root->filePath(QStringLiteral("compatdata/228380/pfx")),
        .identity = {},
        .steamCompatData = m_root->filePath(QStringLiteral("compatdata/228380")),
        .temporaryDirectory = m_root->filePath(QStringLiteral("tmp")),
        .directory = nullptr,
    };
    m_target.identity = winePrefixIdentity(m_target.prefix).value_or(WinePrefixIdentity{});
    writeRegistry(s_machine);
    writeRegistry(s_user, QStringLiteral("user.reg"));
}

QByteArray WineDesktopWriteTest::registry(const QString &name) const
{
    QFile file(m_target.prefix + QStringLiteral("/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void WineDesktopWriteTest::writeRegistry(const QByteArray &text, const QString &name) const
{
    QFile file(m_target.prefix + QStringLiteral("/") + name);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
}

void WineDesktopWriteTest::describesAndUndescribesTheScreen()
{
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
    QCOMPARE(wineScreens(registry()), s_screens);
    QVERIFY(registry().startsWith(s_machine));
    // The user's own registry is not touched for this.
    QCOMPARE(registry(QStringLiteral("user.reg")), s_user);
    QCOMPARE(wineClearScreen(m_target, ::getuid()), WineWriteResult::Written);
    QCOMPARE(registry(), s_machine);
    QCOMPARE(wineClearScreen(m_target, ::getuid()), WineWriteResult::Written);
}

void WineDesktopWriteTest::describesAnotherSizeOverItsOwn()
{
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_smaller, 1790000001), WineWriteResult::Written);
    QCOMPARE(wineScreens(registry()), s_smaller);
    QVERIFY(registry().startsWith(s_machine));
}

void WineDesktopWriteTest::leavesAPrefixWithAVirtualDesktopAlone()
{
    const QByteArray users = s_user + "\n[Software\\\\Wine\\\\Explorer] 1789805658\n\"Desktop\"=\"shell\"\n";
    writeRegistry(users, QStringLiteral("user.reg"));
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(registry(), s_machine);
    // A size left behind when the user switched their desktop off is theirs too.
    const QByteArray switchedOff = s_user + "\n[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n\"Default\"=\"2560x1440\"\n";
    writeRegistry(switchedOff, QStringLiteral("user.reg"));
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(wineClearScreen(m_target, ::getuid()), WineWriteResult::Written);
    QCOMPARE(registry(), s_machine);
}

void WineDesktopWriteTest::clearsPreparationAfterTheUserEnablesADesktop()
{
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
    const QByteArray users = s_user + "\n[Software\\\\Wine\\\\Explorer] 1789805658\n\"Desktop\"=\"shell\"\n";
    writeRegistry(users, QStringLiteral("user.reg"));
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_smaller, 1790000001), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(wineScreens(registry()), s_screens);
    QCOMPARE(wineClearScreen(m_target, ::getuid()), WineWriteResult::Written);
    QCOMPARE(registry(), s_machine);
    QCOMPARE(registry(QStringLiteral("user.reg")), users);
}

void WineDesktopWriteTest::refusesAPrefixThatDescribesNoDevices()
{
    writeRegistry(QByteArrayLiteral("WINE REGISTRY Version 2\n\n#arch=win64\n"));
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Unreachable);
    QVERIFY(wineScreens(registry()).isEmpty());
    // There is nothing to undo either, and nothing is reported as undone wrongly.
    QCOMPARE(wineClearScreen(m_target, ::getuid()), WineWriteResult::Written);
}

void WineDesktopWriteTest::waitsForARunningServer()
{
    const QString lock = wineServerLockPath(m_target.temporaryDirectory, ::getuid(), m_target.identity);
    QVERIFY(QDir().mkpath(QFileInfo(lock).path()));
    {
        const LockHolder server(lock);
        QVERIFY(server.locked());
        QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Busy);
        QCOMPARE(registry(), s_machine);
    }
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
}

void WineDesktopWriteTest::waitsForProton()
{
    {
        const LockHolder proton(m_target.steamCompatData + QStringLiteral("/pfx.lock"), LockHolder::Kind::Proton);
        QVERIFY(proton.locked());
        QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Busy);
        QCOMPARE(registry(), s_machine);
    }
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
}

void WineDesktopWriteTest::refusesADirectoryThatIsNoLongerTheProvenOne()
{
    // Moved aside rather than deleted, so the new directory cannot reuse the
    // old one's inode.
    QVERIFY(QDir().rename(m_target.prefix, m_target.prefix + QStringLiteral("-old")));
    QVERIFY(QDir().mkpath(m_target.prefix));
    writeRegistry(s_machine);
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Unreachable);
    QCOMPARE(registry(), s_machine);
}

void WineDesktopWriteTest::writesTheHeldDirectoryWhereverItsPathLeads()
{
    // As a container's path that the host does not have: the directory was
    // opened while the game ran, and its path now leads somewhere else.
    m_target.directory = WineDirectory::open(m_target.prefix, m_target.identity);
    QVERIFY(m_target.directory);
    const QString moved = m_target.prefix + QStringLiteral("-moved");
    QVERIFY(QDir().rename(m_target.prefix, moved));
    QVERIFY(QDir().mkpath(m_target.prefix));
    writeRegistry(s_machine);
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
    QCOMPARE(registry(), s_machine);
    QCOMPARE(wineScreensIn(*m_target.directory), s_screens);
    QFile held(moved + QStringLiteral("/system.reg"));
    QVERIFY(held.open(QIODevice::ReadOnly));
    QCOMPARE(wineScreens(held.readAll()), s_screens);
    QCOMPARE(QDir(moved).entryList(QDir::Files), (QStringList{QStringLiteral("system.reg"), QStringLiteral("user.reg")}));
}

void WineDesktopWriteTest::refusesAHeldDirectoryThatWasRemoved()
{
    m_target.directory = WineDirectory::open(m_target.prefix, m_target.identity);
    QVERIFY(m_target.directory);
    QVERIFY(QDir(m_target.prefix).removeRecursively());
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Unreachable);
    QVERIFY(!WineDirectory::open(m_root->filePath(QStringLiteral("compatdata")), m_target.identity));
}

void WineDesktopWriteTest::keepsTheFilesPermissions()
{
    const QString path = m_target.prefix + QStringLiteral("/system.reg");
    const QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup;
    QVERIFY(QFile::setPermissions(path, permissions));
    QCOMPARE(wineSetScreens(m_target, ::getuid(), s_screens, 1790000000), WineWriteResult::Written);
    QCOMPARE(QFileInfo(path).permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser), permissions);
}

QTEST_GUILESS_MAIN(WineDesktopWriteTest)

#include "wine_screen_write_test.moc"
