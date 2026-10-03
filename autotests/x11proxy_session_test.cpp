// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "x11proxy_session_test.h"
#include "session.h"
#include "startup.h"
#include "x11proxy_session_fixture.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>
#include <cstdlib>
#include <memory>

using namespace UpscaleX11Test;

void ProxySessionTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY2(bus.isConnected(), "the test runs on a session bus of its own");
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"), &m_effect, QDBusConnection::ExportAllSlots));
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin")));
    int descriptor[2];
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptor), 0);
    if (UpscaleX11::peerProcess(descriptor[0]) == 0) {
        QSKIP("this platform does not say which process is at the other end of a socket");
    }
    close(descriptor[0]);
    close(descriptor[1]);
    QVERIFY(m_directory.isValid());
}

void ProxySessionTest::cleanup()
{
    m_effect.prefixMayMatch = true;
    m_effect.unselected.clear();
    for (const int descriptor : std::as_const(m_kept)) {
        close(descriptor);
    }
    m_kept.clear();
}

std::unique_ptr<UpscaleX11::Session> ProxySessionTest::startSession(const QString &name)
{
    m_path = QFile::encodeName(m_directory.filePath(name));
    const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    int windowManager[2];
    int wayland[2];
    if (listener < 0 || !socketAddress(m_path, address) || bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(listener, 16) != 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, windowManager) != 0
        || socketpair(AF_UNIX, SOCK_STREAM, 0, wayland) != 0) {
        return nullptr;
    }
    m_kept << windowManager[1] << wayland[1];
    qputenv("WAYLAND_SOCKET", QByteArray::number(wayland[0]));
    auto session = std::make_unique<UpscaleX11::Session>(QCoreApplication::applicationFilePath());
    if (!session->start({QStringLiteral(":9"), QStringLiteral("-listenfd"), QString::number(listener),
                         QStringLiteral("-wm"), QString::number(windowManager[0])})) {
        return nullptr;
    }
    return session;
}

int ProxySessionTest::connectClient(QSize &size)
{
    const int client = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    const QByteArray request = setupRequest();
    // Kept from the other process this test starts, whose end would otherwise
    // hold the connection open after this one closes it.
    if (client < 0 || !socketAddress(m_path, address) || fcntl(client, F_SETFD, FD_CLOEXEC) != 0 || ::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size()) {
        close(client);
        return -1;
    }
    // The relay runs in this thread, so the answer is awaited without
    // blocking it.
    SetupReply reply{};
    std::size_t received = 0;
    const auto answered = [&]() {
        const ssize_t count = recv(client, reply.data() + received, reply.size() - received, MSG_DONTWAIT);
        if (count > 0) {
            received += static_cast<std::size_t>(count);
        }
        return received == reply.size();
    };
    if (!QTest::qWaitFor(answered, 5000)) {
        close(client);
        return -1;
    }
    size = rootSize(reply);
    return client;
}

QByteArray ProxySessionTest::screenOfAnotherProgram()
{
    QProcess other;
    other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--connect"), QString::fromLocal8Bit(m_path)});
    const auto ended = [&other]() {
        return other.state() == QProcess::NotRunning;
    };
    if (!QTest::qWaitFor(ended, 5000) || other.exitStatus() != QProcess::NormalExit || other.exitCode() != 0) {
        return QByteArrayLiteral("failed");
    }
    return other.readAllStandardOutput().trimmed();
}

bool ProxySessionTest::disconnectClient(int client)
{
    shutdown(client, SHUT_WR);
    const auto ended = [client]() {
        char byte;
        return recv(client, &byte, 1, MSG_DONTWAIT) == 0;
    };
    const bool finished = QTest::qWaitFor(ended, 5000);
    close(client);
    return finished;
}

// A process is asked about once, and each connection it opens meanwhile sees
// the same screen. Another process is asked for itself, and a process whose
// connections have all closed is asked again.
void ProxySessionTest::oneProcessIsAskedOnce()
{
    const auto session = startSession(QStringLiteral("X9"));
    QVERIFY(session);
    const int before = m_effect.asked;

    QSize first;
    QSize second;
    const int firstClient = connectClient(first);
    QVERIFY(firstClient >= 0);
    QCOMPARE(m_effect.asked, before + 1);
    QCOMPARE(first, QSize(2560, 1440));
    const int secondClient = connectClient(second);
    QVERIFY(secondClient >= 0);
    QCOMPARE(m_effect.asked, before + 1);
    QCOMPARE(second, first);

    QProcess other;
    other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--connect"), QString::fromLocal8Bit(m_path)});
    QTRY_COMPARE_WITH_TIMEOUT(other.state(), QProcess::NotRunning, 5000);
    QCOMPARE(other.exitCode(), 0);
    QCOMPARE(other.readAllStandardOutput().trimmed(), QByteArrayLiteral("2560x1440"));
    QCOMPARE(m_effect.asked, before + 2);

    QVERIFY(disconnectClient(firstClient));
    QVERIFY(disconnectClient(secondClient));
    QSize third;
    const int thirdClient = connectClient(third);
    QVERIFY(thirdClient >= 0);
    QCOMPARE(m_effect.asked, before + 3);
    QCOMPARE(third, QSize(2560, 1440));
    QVERIFY(disconnectClient(thirdClient));
}

// The effect switched off while the proxy runs, which goes on relaying until
// the next login: KWin is still on the bus, the effect's object is not. A
// program connecting then is told its screen unchanged, a connection made
// before goes on being relayed, and once the effect is back, the next program
// is asked and answered again.
void ProxySessionTest::effectSwitchedOffMidSession()
{
    const auto session = startSession(QStringLiteral("X13"));
    QVERIFY(session);
    QSize first;
    const int firstClient = connectClient(first);
    QVERIFY(firstClient >= 0);
    QCOMPARE(first, QSize(2560, 1440));

    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString path = QStringLiteral("/org/kde/KWin/Effect/Upscale1");
    bus.unregisterObject(path);
    const auto restore = qScopeGuard([this, &bus, &path]() {
        bus.registerObject(path, &m_effect, QDBusConnection::ExportAllSlots);
    });
    QCOMPARE(screenOfAnotherProgram(), QByteArrayLiteral("3840x2160"));
    char byte;
    QCOMPARE(recv(firstClient, &byte, 1, MSG_DONTWAIT), ssize_t(-1));
    QVERIFY(errno == EAGAIN || errno == EWOULDBLOCK);

    QVERIFY(bus.registerObject(path, &m_effect, QDBusConnection::ExportAllSlots));
    QCOMPARE(screenOfAnotherProgram(), QByteArrayLiteral("2560x1440"));
    QVERIFY(disconnectClient(firstClient));
}

int main(int argc, char **argv)
{
    // The server this test starts in place of Xwayland, a client in a process
    // of its own, and a game that runs without connecting.
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--wait") == 0) {
            pause();
            return 0;
        }
    }
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], "-listenfd") == 0) {
            return serve(std::atoi(argv[index + 1]));
        }
        if (std::strcmp(argv[index], "--connect") == 0) {
            return connectOnce(argv[index + 1]);
        }
        if (std::strcmp(argv[index], "--hold") == 0) {
            return connectOnce(argv[index + 1], true);
        }
    }
    QTemporaryDir directory(QDir::tempPath() + QStringLiteral("/proxy-session-XXXXXX"));
    if (!directory.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", directory.path().toUtf8());
    qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8());
    QCoreApplication application(argc, argv);
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup(config, QStringLiteral("Plugins")).writeEntry("upscaleEnabled", true);
    KConfigGroup(config, QStringLiteral("Effect-upscale")).writeEntry("X11Proxy", true);
    config->sync();
    ProxySessionTest test;
    return QTest::qExec(&test, argc, argv);
}
