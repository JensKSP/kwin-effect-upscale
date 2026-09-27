// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "session.h"
#include "startup.h"
#include "wire.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// The whole transport as KWin starts it: listening for clients, asking the
// effect over D-Bus how to answer each one, and relaying to the server it
// started. The server is this program again, answering connection setup the
// way Xwayland does; the effect is a stand-in on the test's own bus.
namespace
{

// The setup a client opens its connection with: little-endian, protocol 11,
// no authorization.
QByteArray setupRequest()
{
    const UpscaleX11::Wire wire;
    QByteArray request(12, '\0');
    request[0] = 'l';
    wire.word(request, 2, 11);
    return request;
}

// One screen of 3840x2160, each connection with its own resource range.
QByteArray setupReply(const UpscaleX11::Wire &wire, quint32 connection)
{
    QByteArray reply(80, '\0');
    reply[0] = 1;
    wire.word(reply, 2, 11);
    wire.word(reply, 6, 18);
    wire.integer(reply, 12, (connection + 1) << 21);
    wire.integer(reply, 16, 0x1fffff);
    reply[28] = 1;
    wire.integer(reply, 40, 42);
    wire.word(reply, 60, 3840);
    wire.word(reply, 62, 2160);
    return reply;
}

bool readFully(int descriptor, char *data, std::size_t size)
{
    while (size) {
        const ssize_t count = read(descriptor, data, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        data += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}

// The stand-in server: answers each connection's setup, then reads until the
// relay closes it.
int serve(int listener)
{
    std::signal(SIGCHLD, SIG_IGN);
    fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) & ~O_NONBLOCK);
    for (quint32 connection = 0;; ++connection) {
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 1;
        }
        if (fork() != 0) {
            close(client);
            continue;
        }
        QByteArray request(12, '\0');
        if (!readFully(client, request.data(), 12)) {
            _exit(1);
        }
        UpscaleX11::Wire wire;
        wire.little = request[0] == 'l';
        const qsizetype authorization = ((wire.word(request, 6) + 3) & ~3) + ((wire.word(request, 8) + 3) & ~3);
        QByteArray discarded(authorization, '\0');
        const QByteArray reply = setupReply(wire, connection);
        if (!readFully(client, discarded.data(), discarded.size()) || write(client, reply.constData(), reply.size()) != reply.size()) {
            _exit(1);
        }
        char byte;
        while (read(client, &byte, 1) > 0) { }
        _exit(0);
    }
}

// A client in another process, which prints the root size it was given.
int connectOnce(const QByteArray &path)
{
    const int client = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.constData(), static_cast<std::size_t>(path.size() + 1));
    const QByteArray request = setupRequest();
    QByteArray reply(80, '\0');
    if (::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size() || !readFully(client, reply.data(), 80)) {
        return 1;
    }
    const UpscaleX11::Wire wire;
    std::printf("%ux%u\n", static_cast<unsigned>(wire.word(reply, 60)), static_cast<unsigned>(wire.word(reply, 62)));
    return 0;
}

} // namespace

// The effect's side of the question, counting how often it is asked.
class EffectStandIn : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Effect.Upscale1")
public:
    int asked = 0;
public Q_SLOTS:
    QVariantMap x11ConnectionPolicy(uint pid, const QStringList &candidates)
    {
        Q_UNUSED(pid)
        Q_UNUSED(candidates)
        ++asked;
        const UpscaleX11::Wire canonical;
        QByteArray timing(32, '\0');
        canonical.integer(timing, 0, 1);
        canonical.word(timing, 4, 2560);
        canonical.word(timing, 6, 1440);
        return {{QStringLiteral("width"), 2560},
                {QStringLiteral("height"), 1440},
                {QStringLiteral("timing"), timing},
                {QStringLiteral("reason"), QStringLiteral("connection display advertisement")}};
    }
};

class ProxySessionTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void oneProcessIsAskedOnce();

private:
    // Opens a connection, sends its setup, and waits for the answer. Returns
    // the descriptor, with the root size the client was told in @p size.
    int connectClient(QSize &size);
    // Closes a connection once its relay has finished: the relay passes the
    // end of the stream back in the same step in which it finishes.
    bool disconnectClient(int client);

    QTemporaryDir m_directory;
    QByteArray m_path;
    EffectStandIn m_effect;
};

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
    m_path = QFile::encodeName(m_directory.filePath(QStringLiteral("X9")));
}

int ProxySessionTest::connectClient(QSize &size)
{
    const int client = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, m_path.constData(), static_cast<std::size_t>(m_path.size() + 1));
    const QByteArray request = setupRequest();
    // Kept from the other process this test starts, whose end would otherwise
    // hold the connection open after this one closes it.
    if (fcntl(client, F_SETFD, FD_CLOEXEC) != 0 || ::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size()) {
        close(client);
        return -1;
    }
    // The relay runs in this thread, so the answer is awaited without
    // blocking it.
    QByteArray reply;
    const auto answered = [&]() {
        char data[80];
        const ssize_t count = recv(client, data, sizeof(data) - static_cast<std::size_t>(reply.size()), MSG_DONTWAIT);
        if (count > 0) {
            reply.append(data, count);
        }
        return reply.size() == 80;
    };
    if (!QTest::qWaitFor(answered, 5000)) {
        close(client);
        return -1;
    }
    const UpscaleX11::Wire wire;
    size = QSize(wire.word(reply, 60), wire.word(reply, 62));
    return client;
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
    const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, m_path.constData(), static_cast<std::size_t>(m_path.size() + 1));
    QCOMPARE(bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)), 0);
    QCOMPARE(::listen(listener, 16), 0);
    int windowManager[2];
    int wayland[2];
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, windowManager), 0);
    QCOMPARE(socketpair(AF_UNIX, SOCK_STREAM, 0, wayland), 0);
    qputenv("WAYLAND_SOCKET", QByteArray::number(wayland[0]));
    UpscaleX11::Session session(QCoreApplication::applicationFilePath());
    QVERIFY(session.start({QStringLiteral(":9"), QStringLiteral("-listenfd"), QString::number(listener),
                           QStringLiteral("-wm"), QString::number(windowManager[0])}));

    QSize first;
    QSize second;
    const int firstClient = connectClient(first);
    QVERIFY(firstClient >= 0);
    QCOMPARE(m_effect.asked, 1);
    QCOMPARE(first, QSize(2560, 1440));
    const int secondClient = connectClient(second);
    QVERIFY(secondClient >= 0);
    QCOMPARE(m_effect.asked, 1);
    QCOMPARE(second, first);

    QProcess other;
    other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--connect"), QString::fromLocal8Bit(m_path)});
    QTRY_COMPARE_WITH_TIMEOUT(other.state(), QProcess::NotRunning, 5000);
    QCOMPARE(other.exitCode(), 0);
    QCOMPARE(other.readAllStandardOutput().trimmed(), QByteArrayLiteral("2560x1440"));
    QCOMPARE(m_effect.asked, 2);

    QVERIFY(disconnectClient(firstClient));
    QVERIFY(disconnectClient(secondClient));
    QSize third;
    const int thirdClient = connectClient(third);
    QVERIFY(thirdClient >= 0);
    QCOMPARE(m_effect.asked, 3);
    QCOMPARE(third, QSize(2560, 1440));
    QVERIFY(disconnectClient(thirdClient));
    close(windowManager[1]);
    close(wayland[1]);
}

int main(int argc, char **argv)
{
    // The server this test starts in place of Xwayland, and a client in a
    // process of its own.
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], "-listenfd") == 0) {
            return serve(std::atoi(argv[index + 1]));
        }
        if (std::strcmp(argv[index], "--connect") == 0) {
            return connectOnce(argv[index + 1]);
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
#include "x11proxy_session_test.moc"
