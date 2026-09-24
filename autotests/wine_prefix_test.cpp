/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lockholder.h"
#include "wineprefix.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

namespace
{

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

} // namespace

class WinePrefixTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void namesTheLockAsWineDoes();
    void seesAServerHoldTheLock();
    void locatesThePrefixItsServerHolds();
    void fallsBackToWinesDefaultBelowHome();
    void refusesWhatItCannotProve();
    void refusesARegistryItCannotReplace();
    void crossChecksProton();
    void readsARunningProcess();
    void namesAnUntestedWineBuild();

private:
    WineProcess process(const QString &prefix) const;
    QString lockPath(const QString &prefix) const;

    std::optional<QTemporaryDir> m_root;
};

void WinePrefixTest::init()
{
    m_root.emplace();
    QVERIFY(m_root->isValid());
    writeFile(m_root->filePath(QStringLiteral("pfx/user.reg")), QByteArrayLiteral("WINE REGISTRY Version 2\n"));
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("tmp"))));
}

WineProcess WinePrefixTest::process(const QString &prefix) const
{
    WineProcess result{
        .owner = ::getuid(),
        .root = m_root->path(),
        .executable = {},
        .arguments = {},
        .workingDirectory = {},
        .environment = {},
    };
    if (!prefix.isNull()) {
        result.environment.insert(QStringLiteral("WINEPREFIX"), prefix);
    }
    return result;
}

QString WinePrefixTest::lockPath(const QString &prefix) const
{
    const std::optional<WinePrefixIdentity> identity = winePrefixIdentity(m_root->path() + prefix);
    return identity ? wineServerLockPath(m_root->filePath(QStringLiteral("tmp")), ::getuid(), *identity) : QString();
}

void WinePrefixTest::namesTheLockAsWineDoes()
{
    const WinePrefixIdentity identity{.device = 0x803, .inode = 0x1a2b};
    QCOMPARE(wineServerLockPath(QStringLiteral("/tmp"), 1000, identity), QStringLiteral("/tmp/.wine-1000/server-803-1a2b/lock"));
    QVERIFY(!winePrefixIdentity(m_root->filePath(QStringLiteral("pfx/user.reg"))));
}

void WinePrefixTest::seesAServerHoldTheLock()
{
    const QString path = lockPath(QStringLiteral("/pfx"));
    QCOMPARE(wineServerState(path), WineServerState::Stopped);
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    {
        const LockHolder server(path);
        QVERIFY(server.locked());
        QCOMPARE(wineServerState(path), WineServerState::Running);
        QCOMPARE(wineServerProcess(path), server.process());
        QVERIFY(wineProcessExists(server.process()));
    }
    QVERIFY(QFile::exists(path));
    QCOMPARE(wineServerState(path), WineServerState::Stopped);
    QVERIFY(!wineServerProcess(path));
}

void WinePrefixTest::locatesThePrefixItsServerHolds()
{
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/pfx")), ::getuid(), QString()).error(), WinePrefixRefusal::ServerNotRunning);
    QVERIFY(QDir().mkpath(QFileInfo(lockPath(QStringLiteral("/pfx"))).path()));
    const LockHolder server(lockPath(QStringLiteral("/pfx")));
    QVERIFY(server.locked());
    const auto prefix = wineLocatePrefix(process(QStringLiteral("/pfx/./")), ::getuid(), QStringLiteral("explorer.exe"));
    QVERIFY(prefix);
    QCOMPARE(prefix->gamePath, QStringLiteral("/pfx"));
    QCOMPARE(prefix->path, m_root->filePath(QStringLiteral("pfx")));
    QCOMPARE(wineRegistryPath(*prefix), m_root->filePath(QStringLiteral("pfx/user.reg")));
    QVERIFY(prefix->steamAppId.isEmpty());
}

void WinePrefixTest::fallsBackToWinesDefaultBelowHome()
{
    writeFile(m_root->filePath(QStringLiteral("home/player/.wine/user.reg")), QByteArrayLiteral("WINE REGISTRY Version 2\n"));
    QVERIFY(QDir().mkpath(QFileInfo(lockPath(QStringLiteral("/home/player/.wine"))).path()));
    const LockHolder server(lockPath(QStringLiteral("/home/player/.wine")));
    WineProcess game = process(QString());
    game.environment.insert(QStringLiteral("HOME"), QStringLiteral("/home/player"));
    const auto prefix = wineLocatePrefix(game, ::getuid(), QString());
    QVERIFY(prefix);
    QCOMPARE(prefix->gamePath, QStringLiteral("/home/player/.wine"));
}

void WinePrefixTest::refusesWhatItCannotProve()
{
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/pfx")), ::getuid() + 1, QString()).error(), WinePrefixRefusal::OtherUser);
    QCOMPARE(wineLocatePrefix(process(QString()), ::getuid(), QString()).error(), WinePrefixRefusal::NoPrefix);
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("pfx")), ::getuid(), QString()).error(), WinePrefixRefusal::NoPrefix);
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/missing")), ::getuid(), QString()).error(), WinePrefixRefusal::NotADirectory);
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("empty"))));
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/empty")), ::getuid(), QString()).error(), WinePrefixRefusal::RegistryMissing);
    writeFile(m_root->filePath(QStringLiteral("other/user.reg")), QByteArrayLiteral("REGEDIT4\n"));
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/other")), ::getuid(), QString()).error(), WinePrefixRefusal::NotARegistry);
}

void WinePrefixTest::refusesARegistryItCannotReplace()
{
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("linked"))));
    QVERIFY(QFile::link(m_root->filePath(QStringLiteral("pfx/user.reg")), m_root->filePath(QStringLiteral("linked/user.reg"))));
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/linked")), ::getuid(), QString()).error(), WinePrefixRefusal::RegistryNotPlain);
    QVERIFY(QDir().mkpath(m_root->filePath(QStringLiteral("hard"))));
    QCOMPARE(::link(QFile::encodeName(m_root->filePath(QStringLiteral("pfx/user.reg"))).constData(),
                    QFile::encodeName(m_root->filePath(QStringLiteral("hard/user.reg"))).constData()),
             0);
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/hard")), ::getuid(), QString()).error(), WinePrefixRefusal::RegistryNotPlain);
}

void WinePrefixTest::crossChecksProton()
{
    const QString gamePrefix = QStringLiteral("/steam/compatdata/228380/pfx");
    writeFile(m_root->path() + gamePrefix + QStringLiteral("/user.reg"), QByteArrayLiteral("WINE REGISTRY Version 2\n"));
    QVERIFY(QDir().mkpath(QFileInfo(lockPath(gamePrefix)).path()));
    const LockHolder server(lockPath(gamePrefix));
    WineProcess game = process(gamePrefix);
    game.environment.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"), QStringLiteral("/steam/compatdata/228380"));
    game.environment.insert(QStringLiteral("SteamAppId"), QStringLiteral("228380"));
    const auto prefix = wineLocatePrefix(game, ::getuid(), QStringLiteral("steam_app_228380"));
    QVERIFY(prefix);
    QCOMPARE(prefix->steamAppId, QStringLiteral("228380"));
    QCOMPARE(prefix->steamCompatDataPath, m_root->path() + QStringLiteral("/steam/compatdata/228380"));
    QCOMPARE(wineLocatePrefix(game, ::getuid(), QStringLiteral("steam_app_550")).error(), WinePrefixRefusal::SteamMismatch);
    // A launcher other than Steam that uses Proton names its compatibility
    // data as it likes: Proton's layout still holds, but Steam cannot start
    // that game again.
    game.environment.insert(QStringLiteral("SteamAppId"), QStringLiteral("umu-default"));
    const auto other = wineLocatePrefix(game, ::getuid(), QString());
    QVERIFY(other);
    QVERIFY(other->steamAppId.isEmpty());
    QCOMPARE(other->steamCompatDataPath, m_root->path() + QStringLiteral("/steam/compatdata/228380"));
    QCOMPARE(wineLocatePrefix(process(QStringLiteral("/pfx")), ::getuid(), QStringLiteral("steam_app_228380")).error(), WinePrefixRefusal::SteamMismatch);
}

void WinePrefixTest::readsARunningProcess()
{
    const std::optional<WineProcess> self = wineProcess(QCoreApplication::applicationPid());
    if (!self) {
        QSKIP("This platform cannot read another process's environment.");
    }
    QCOMPARE(self->owner, ::getuid());
    QCOMPARE(QFileInfo(self->executable).canonicalFilePath(), QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath());
    QCOMPARE(self->arguments.value(0), QCoreApplication::arguments().value(0));
    QCOMPARE(self->workingDirectory, QDir::currentPath());
    QCOMPARE(self->environment.value(QStringLiteral("PATH")), qEnvironmentVariable("PATH"));
    QVERIFY(QFileInfo::exists(self->root + QCoreApplication::applicationFilePath()));
}

void WinePrefixTest::namesAnUntestedWineBuild()
{
    // Proton writes its build beside the prefix; plain Wine writes nothing.
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(wineBuild(root.path()).isEmpty());
    QVERIFY(wineBuild(QString()).isEmpty());
    QFile version(root.filePath(QStringLiteral("version")));
    QVERIFY(version.open(QIODevice::WriteOnly));
    version.write("11.0-100\n");
    version.close();
    QCOMPARE(wineBuild(root.path()), QStringLiteral("11.0-100"));

    // The build the description was tested against, and the ones it was not:
    // each of those is written for all the same, and named in the log.
    QVERIFY(wineBuildTested(QStringLiteral("11.0-100")));
    QVERIFY(wineBuildTested(QStringLiteral("experimental-11.0-20260917b")));
    QVERIFY(!wineBuildTested(QStringLiteral("12.0-1")));
    QVERIFY(!wineBuildTested(QStringLiteral("10.0-4b")));
    QVERIFY(!wineBuildTested(QString()));
}

QTEST_GUILESS_MAIN(WinePrefixTest)

#include "wine_prefix_test.moc"
