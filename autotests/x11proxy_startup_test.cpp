/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "startup.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <cerrno>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
class ProxyStartupTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void eitherSwitchDisablesRouting();
    void authenticatedPeer();
    void namesTheArgumentItCannotUse();
};
void ProxyStartupTest::eitherSwitchDisablesRouting()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    for (const bool effect : {false, true}) {
        for (const bool proxy : {false, true}) {
            KConfigGroup(config, QStringLiteral("Plugins")).writeEntry("upscaleEnabled", effect);
            KConfigGroup(config, QStringLiteral("Effect-upscale")).writeEntry("X11Proxy", proxy);
            QVERIFY(config->sync());
            QCOMPARE(UpscaleX11::requestedRouting(), effect && proxy);
        }
    }
}
void ProxyStartupTest::authenticatedPeer()
{
    int pair[2];
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    const quint32 pid = UpscaleX11::peerProcess(pair[0]);
    close(pair[0]);
    close(pair[1]);
#if defined(Q_OS_LINUX)
    QCOMPARE(pid, static_cast<quint32>(getpid()));
#else
    QVERIFY(pid == 0 || pid == static_cast<quint32>(getpid()));
#endif
}
// Each refusal names the argument it could not use, and the system's reason only
// where a call gave one.
void ProxyStartupTest::namesTheArgumentItCannotUse()
{
    int pair[2];
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    const QString open = QString::number(pair[0]);
    qunsetenv("WAYLAND_SOCKET");
    const QString prefix = QStringLiteral("Cannot initialize Upscale X11 session: ");
    const auto refused = [&](const QStringList &arguments, const QString &message) {
        QTest::ignoreMessage(QtCriticalMsg, qPrintable(prefix + message));
        UpscaleX11::Startup startup;
        return !startup.parse(arguments, pair[1]);
    };
    QVERIFY(refused({QStringLiteral("-wm")}, QStringLiteral("-wm: no descriptor follows it")));
    QVERIFY(refused({QStringLiteral("-wm"), QStringLiteral("x")}, QStringLiteral("-wm: x is not a descriptor")));
    QVERIFY(refused({QStringLiteral("-wm"), open}, QStringLiteral("-listenfd: missing")));
    QVERIFY(refused({QStringLiteral("-listenfd"), open}, QStringLiteral("-wm: missing")));
    QVERIFY(refused({QStringLiteral("-listenfd"), open, QStringLiteral("-wm"), open}, QStringLiteral("WAYLAND_SOCKET: not set to a descriptor")));
    close(pair[0]);
    const QString closed = QString::fromStdString(std::error_code(EBADF, std::generic_category()).message());
    QVERIFY(refused({QStringLiteral("-wm"), open}, QStringLiteral("-wm %1: %2").arg(open, closed)));
    close(pair[1]);
}
int main(int argc, char **argv)
{
    QTemporaryDir directory(QStringLiteral("proxy-settings-XXXXXX"));
    if (!directory.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", directory.path().toUtf8());
    QCoreApplication application(argc, argv);
    ProxyStartupTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "x11proxy_startup_test.moc"
