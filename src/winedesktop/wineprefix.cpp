/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineprefix.h"
#include "wineregistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>

namespace
{

const QString steamAppClass = QStringLiteral("steam_app_");

WineLocated refused(WinePrefixRefusal refusal)
{
    return {.prefix = {}, .refusal = refusal, .located = false};
}

// The prefix as the game names it: WINEPREFIX, or Wine's default below HOME.
QString gamePrefixPath(const QProcessEnvironment &environment)
{
    QString path = environment.value(QStringLiteral("WINEPREFIX"));
    if (path.isEmpty()) {
        const QString home = environment.value(QStringLiteral("HOME"));
        if (home.isEmpty()) {
            return {};
        }
        path = home + QStringLiteral("/.wine");
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        return {};
    }
    return QDir::cleanPath(path);
}

} // namespace

std::optional<WinePrefixRefusal> wineCheckRegistry(const QString &path, uid_t user)
{
    struct stat status = {};
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0) {
        return WinePrefixRefusal::RegistryMissing;
    }
    if (!S_ISREG(status.st_mode) || status.st_nlink != 1) {
        return WinePrefixRefusal::RegistryNotPlain;
    }
    if (status.st_uid != user) {
        return WinePrefixRefusal::NotOwned;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !wineIsRegistry(file.read(64))) {
        return WinePrefixRefusal::NotARegistry;
    }
    return std::nullopt;
}

namespace
{

// Proton's variables, where they are set. A window named steam_app_<id> has
// to name the application SteamAppId names. A prefix that is pfx in the
// compatibility data is laid out by Proton, whose lock on that directory
// applies; any launcher using Proton does that, and only Steam's own layout,
// where the directory is named after the application, is one Steam can start
// again.
std::optional<WinePrefixRefusal> checkSteam(WinePrefix &prefix, const QProcessEnvironment &environment, const QString &windowClass)
{
    const QString appId = environment.value(QStringLiteral("SteamAppId"));
    if (windowClass.startsWith(steamAppClass) && windowClass != steamAppClass + appId) {
        return WinePrefixRefusal::SteamMismatch;
    }
    const QString compatData = environment.value(QStringLiteral("STEAM_COMPAT_DATA_PATH"));
    if (compatData.isEmpty() || prefix.gamePath != QDir::cleanPath(compatData) + QStringLiteral("/pfx")) {
        return std::nullopt;
    }
    prefix.steamCompatDataPath = prefix.path.chopped(QStringLiteral("/pfx").size());
    if (!appId.isEmpty() && QFileInfo(QDir::cleanPath(compatData)).fileName() == appId) {
        prefix.steamAppId = appId;
    }
    return std::nullopt;
}

} // namespace

WineLocated wineLocatePrefix(const WineProcess &process, uid_t user, const QString &windowClass)
{
    if (process.owner != user) {
        return refused(WinePrefixRefusal::OtherUser);
    }
    WinePrefix prefix;
    prefix.gamePath = gamePrefixPath(process.environment);
    if (prefix.gamePath.isEmpty()) {
        return refused(WinePrefixRefusal::NoPrefix);
    }
    prefix.path = process.root + prefix.gamePath;
    prefix.temporaryDirectory = process.root + QStringLiteral("/tmp");
    const std::optional<WinePrefixIdentity> identity = winePrefixIdentity(prefix.path);
    if (!identity) {
        return refused(WinePrefixRefusal::NotADirectory);
    }
    prefix.identity = *identity;
    struct stat status = {};
    if (::stat(QFile::encodeName(prefix.path).constData(), &status) != 0 || status.st_uid != user) {
        return refused(WinePrefixRefusal::NotOwned);
    }
    if (const std::optional<WinePrefixRefusal> refusal = wineCheckRegistry(wineRegistryPath(prefix), user)) {
        return refused(*refusal);
    }
    if (const std::optional<WinePrefixRefusal> refusal = checkSteam(prefix, process.environment, windowClass)) {
        return refused(*refusal);
    }
    if (wineServerState(wineServerLockPath(prefix.temporaryDirectory, user, prefix.identity)) != WineServerState::Running) {
        return refused(WinePrefixRefusal::ServerNotRunning);
    }
    return {.prefix = prefix, .refusal = {}, .located = true};
}

QString wineRegistryPath(const WinePrefix &prefix)
{
    return prefix.path + QStringLiteral("/user.reg");
}
