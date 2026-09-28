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
#include <sys/socket.h>
#include <unistd.h>
class ProxyStartupTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void eitherSwitchDisablesRouting();
    void authenticatedPeer();
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
