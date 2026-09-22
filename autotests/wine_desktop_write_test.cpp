/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lockholder.h"
#include "winedesktopwrite.h"
#include "wineregistry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace
{

const QByteArray s_registry = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n"
    "\n"
    "#arch=win64\n"
    "\n"
    "[Control Panel\\\\Desktop] 1789805658\n"
    "\"ActiveWndTrackTimeout\"=dword:00000000\n");

const QSize s_size(2560, 1440);

} // namespace

class WineDesktopWriteTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void setsAndClearsItsDesktop();
    void replacesOnlyItsOwnDesktop();
    void leavesTheUsersDesktopAlone();
    void waitsForARunningServer();
    void waitsForProton();
    void refusesADirectoryThatIsNoLongerTheProvenOne();
    void keepsTheFilesPermissions();

private:
    QByteArray registry() const;
    void writeRegistry(const QByteArray &text) const;

    std::optional<QTemporaryDir> m_root;
    WineDesktopTarget m_target;
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
    };
    m_target.identity = winePrefixIdentity(m_target.prefix).value_or(WinePrefixIdentity{});
    writeRegistry(s_registry);
}

QByteArray WineDesktopWriteTest::registry() const
{
    QFile file(m_target.prefix + QStringLiteral("/user.reg"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void WineDesktopWriteTest::writeRegistry(const QByteArray &text) const
{
    QFile file(m_target.prefix + QStringLiteral("/user.reg"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
}

void WineDesktopWriteTest::setsAndClearsItsDesktop()
{
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Written);
    QCOMPARE(wineDesktopValues(registry()), (WineDesktopValues{.desktop = QStringLiteral("Default"), .defaultSize = QStringLiteral("2560x1440")}));
    QVERIFY(registry().startsWith(s_registry));
    QCOMPARE(wineClearDesktop(m_target, ::getuid(), s_size), WineWriteResult::Written);
    QCOMPARE(wineDesktopValues(registry()), WineDesktopValues{});
    QVERIFY(registry().startsWith(s_registry));
    QCOMPARE(wineClearDesktop(m_target, ::getuid(), s_size), WineWriteResult::Written);
}

void WineDesktopWriteTest::replacesOnlyItsOwnDesktop()
{
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Written);
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), QSize(1920, 1080), s_size, 1790000001), WineWriteResult::Written);
    QCOMPARE(wineDesktopValues(registry()).defaultSize, QStringLiteral("1920x1080"));
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000002), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(wineClearDesktop(m_target, ::getuid(), s_size), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(wineDesktopValues(registry()).defaultSize, QStringLiteral("1920x1080"));
}

void WineDesktopWriteTest::leavesTheUsersDesktopAlone()
{
    const QByteArray users = s_registry + "\n[Software\\\\Wine\\\\Explorer] 1789805658\n\"Desktop\"=\"shell\"\n";
    writeRegistry(users);
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, s_size, 1790000000), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(registry(), users);
    // A size left behind when the user switched their desktop off is theirs too.
    const QByteArray switchedOff = s_registry + "\n[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n\"Default\"=\"2560x1440\"\n";
    writeRegistry(switchedOff);
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, s_size, 1790000000), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(wineClearDesktop(m_target, ::getuid(), s_size), WineWriteResult::DesktopOfTheUser);
    QCOMPARE(registry(), switchedOff);
}

void WineDesktopWriteTest::waitsForARunningServer()
{
    const QString lock = wineServerLockPath(m_target.temporaryDirectory, ::getuid(), m_target.identity);
    QVERIFY(QDir().mkpath(QFileInfo(lock).path()));
    {
        const LockHolder server(lock);
        QVERIFY(server.locked());
        QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Busy);
        QCOMPARE(registry(), s_registry);
    }
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Written);
}

void WineDesktopWriteTest::waitsForProton()
{
    {
        const LockHolder proton(m_target.steamCompatData + QStringLiteral("/pfx.lock"), LockHolder::Kind::Proton);
        QVERIFY(proton.locked());
        QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Busy);
        QCOMPARE(registry(), s_registry);
    }
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Written);
}

void WineDesktopWriteTest::refusesADirectoryThatIsNoLongerTheProvenOne()
{
    // Moved aside rather than deleted, so the new directory cannot reuse the
    // old one's inode.
    QVERIFY(QDir().rename(m_target.prefix, m_target.prefix + QStringLiteral("-old")));
    QVERIFY(QDir().mkpath(m_target.prefix));
    writeRegistry(s_registry);
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Unreachable);
    QCOMPARE(registry(), s_registry);
}

void WineDesktopWriteTest::keepsTheFilesPermissions()
{
    const QString path = m_target.prefix + QStringLiteral("/user.reg");
    const QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup;
    QVERIFY(QFile::setPermissions(path, permissions));
    QCOMPARE(wineSetDesktop(m_target, ::getuid(), s_size, std::nullopt, 1790000000), WineWriteResult::Written);
    QCOMPARE(QFileInfo(path).permissions() & ~(QFileDevice::ReadUser | QFileDevice::WriteUser), permissions);
}

QTEST_GUILESS_MAIN(WineDesktopWriteTest)

#include "wine_desktop_write_test.moc"
