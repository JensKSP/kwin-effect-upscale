/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wine_screen_helper_test.h"

namespace
{

const QByteArray s_user = QByteArrayLiteral("WINE REGISTRY Version 2\n\n[Control Panel\\\\Desktop] 1789805658\n");
// A prefix that has run once and kept the devices it found.
const QByteArray s_machine = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0000&0000] 1790154356\n"
    "\"DeviceDesc\"=\"Generic Non-PnP Monitor\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000\\\\Device Parameters] 1790154350\n"
    "\"VideoID\"=\"{65aaded5-ba18-41e2-9572-7290231b645a}\"\n");

} // namespace

void WineScreenHelperTest::init()
{
    if (!wineProcess(QCoreApplication::applicationPid())) {
        QSKIP("This platform cannot read another process's environment.");
    }
    m_root.emplace();
    QVERIFY(m_root->isValid());
    m_prefix = m_root->filePath(QStringLiteral("compatdata/228380/pfx"));
    QVERIFY(QDir().mkpath(m_prefix));
    writePrefix();
    const std::optional<WinePrefixIdentity> identity = winePrefixIdentity(m_prefix);
    QVERIFY(identity);
    m_lockDirectory = QFileInfo(wineServerLockPath(QStringLiteral("/tmp"), ::getuid(), *identity)).path();
    QVERIFY(QDir().mkpath(m_lockDirectory));
    m_records.emplace(m_root->filePath(QStringLiteral("records")));
    m_helper.emplace(&*m_records);
    m_helper->setTiming(std::chrono::milliseconds(20), std::chrono::milliseconds(200));
    m_launched.clear();
    m_helper->setLauncher([this](const WineScreenRecord &record) {
        m_launched.append(record.steamAppId);
        return true;
    });
}

void WineScreenHelperTest::cleanup()
{
    m_helper.reset();
    QDir(m_lockDirectory).removeRecursively();
}

std::unique_ptr<QProcess> WineScreenHelperTest::startGame() const
{
    auto game = std::make_unique<QProcess>();
    QProcessEnvironment environment;
    environment.insert(QStringLiteral("WINEPREFIX"), m_prefix);
    environment.insert(QStringLiteral("STEAM_COMPAT_DATA_PATH"), m_root->filePath(QStringLiteral("compatdata/228380")));
    environment.insert(QStringLiteral("SteamAppId"), QStringLiteral("228380"));
    game->setProcessEnvironment(environment);
    game->start(QStringLiteral("sleep"), {QStringLiteral("60")});
    game->waitForStarted();
    // QProcess reports the start before the new program's environment is in
    // place; a real game has long been running when the effect asks.
    const bool ready = QTest::qWaitFor(
        [&game] {
        const std::optional<WineProcess> process = wineProcess(game->processId());
        return process && process->environment.contains(QStringLiteral("WINEPREFIX"));
    },
        5000);
    Q_UNUSED(ready)
    return game;
}

std::unique_ptr<LockHolder> WineScreenHelperTest::startServer() const
{
    return std::make_unique<LockHolder>(m_lockDirectory + QStringLiteral("/lock"));
}

std::optional<QSize> WineScreenHelperTest::screen() const
{
    QFile registry(m_prefix + QStringLiteral("/system.reg"));
    if (!registry.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QList<WineScreen> screens = wineScreens(registry.readAll());
    return screens.isEmpty() ? std::nullopt : std::optional(screens.first().rect.size());
}

// The prefix as Wine leaves it: the devices it found, and no screen of ours.
void WineScreenHelperTest::writePrefix() const
{
    for (const auto &[name, text] : {std::pair{QStringLiteral("/system.reg"), s_machine}, std::pair{QStringLiteral("/user.reg"), s_user}}) {
        QFile registry(m_prefix + name);
        QVERIFY(registry.open(QIODevice::WriteOnly | QIODevice::Truncate));
        registry.write(text);
    }
}
