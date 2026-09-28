// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later

// How the session ends, and what it keeps while it runs. The session is the
// production one, started as KWin starts it, in front of the stand-in server
// of x11proxy_session_fixture.h: in a process of its own for the cases about
// signals, so that they are sent to it and not to the test, and in this one
// for counting its descriptors.

#include "session.h"
#include "x11proxy_session_fixture.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <memory>

using namespace UpscaleX11Test;

namespace
{

// Descriptors this process has open, counted without /proc: each one below
// the process's limit that fcntl() knows.
int openDescriptors()
{
    const long limit = std::min(sysconf(_SC_OPEN_MAX), 1L << 20);
    int open = 0;
    for (int descriptor = 0; descriptor < limit; ++descriptor) {
        open += fcntl(descriptor, F_GETFD) != -1;
    }
    return open;
}

// What KWin hands a session: the socket clients connect to at @p path, its
// window manager's connection and its Wayland one. The other ends of the two
// pairs are KWin's, and stay open for as long as the session runs.
QStringList listenAt(const QByteArray &path, QList<int> &kept)
{
    const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    int windowManager[2];
    int wayland[2];
    if (listener < 0 || !socketAddress(path, address) || bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(listener, 16) != 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, windowManager) != 0
        || socketpair(AF_UNIX, SOCK_STREAM, 0, wayland) != 0) {
        return {};
    }
    kept << windowManager[1] << wayland[1];
    qputenv("WAYLAND_SOCKET", QByteArray::number(wayland[0]));
    return {QStringLiteral(":9"), QStringLiteral("-listenfd"), QString::number(listener), QStringLiteral("-wm"), QString::number(windowManager[0])};
}

// A session in a process of its own, until it is told to end.
int runSession(const QByteArray &path)
{
    QList<int> kept;
    const QStringList arguments = listenAt(path, kept);
    UpscaleX11::Session session(QCoreApplication::applicationFilePath());
    if (arguments.isEmpty() || !session.start(arguments)) {
        return 1;
    }
    return QCoreApplication::exec();
}

// A client that has been answered, or -1. Blocks, bounded, which only a test
// whose session runs in another process may do.
int connectBlocking(const QByteArray &path)
{
    const int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    const timeval bound{5, 0};
    const QByteArray request = setupRequest();
    QByteArray reply(80, '\0');
    if (client < 0 || !socketAddress(path, address) || setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &bound, sizeof(bound)) != 0
        || ::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size() || !readFully(client, reply.data(), 80)) {
        close(client);
        return -1;
    }
    return client;
}

// A client answered by a session in this thread, which has to keep running
// while the answer is awaited.
int connectHere(const QByteArray &path)
{
    const int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un address{};
    const QByteArray request = setupRequest();
    if (client < 0 || !socketAddress(path, address) || ::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size()) {
        close(client);
        return -1;
    }
    qsizetype received = 0;
    const auto answered = [&]() {
        char data[80];
        const ssize_t count = recv(client, data, sizeof(data) - static_cast<std::size_t>(received), MSG_DONTWAIT);
        received += std::max<ssize_t>(count, 0);
        return received == 80;
    };
    if (!QTest::qWaitFor(answered, 5000)) {
        close(client);
        return -1;
    }
    return client;
}

// Ends a client once its relay has finished, which passes the end of the
// stream back in the step in which it does.
bool disconnectHere(int client)
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

bool gone(pid_t pid)
{
    return kill(pid, 0) == -1 && errno == ESRCH;
}

} // namespace

class ProxyShutdownTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void terminationEndsServerAndSession();
    void aServerIgnoringTerminationIsKilled();
    void connectionsLeaveNoDescriptorsBehind();

private:
    // Starts a session in another process, listening at @p name, and returns
    // its server's PID, or 0.
    pid_t startSession(QProcess &session, const QString &name, bool serverIgnoresTermination);
    // Whether a session has left its socket directory behind.
    bool socketsLeft() const;
    // What a session said and how it ended, for a failure to show.
    QByteArray outcome(QProcess &session) const;

    QTemporaryDir m_directory;
    QByteArray m_path;
};

void ProxyShutdownTest::initTestCase()
{
    QVERIFY(m_directory.isValid());
}

pid_t ProxyShutdownTest::startSession(QProcess &session, const QString &name, bool serverIgnoresTermination)
{
    m_path = QFile::encodeName(m_directory.filePath(name));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (serverIgnoresTermination) {
        environment.insert(QStringLiteral("UPSCALE_TEST_SERVER_IGNORES_TERM"), QStringLiteral("1"));
    }
    session.setProcessEnvironment(environment);
    session.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--session"), QString::fromLocal8Bit(m_path)});
    // The session names its server as it starts it, and a server that
    // ignores SIGTERM says so once it does: a signal sent before then would
    // end it, which a slow start such as ThreadSanitizer's makes likely.
    const QRegularExpression started(QStringLiteral("backend started pid=\\s*(\\d+)"));
    const QString ignoring = QStringLiteral("server ignores SIGTERM");
    QString log;
    const auto named = [&]() {
        log += QString::fromLocal8Bit(session.readAllStandardError());
        const bool ready = started.match(log).hasMatch() && (!serverIgnoresTermination || log.contains(ignoring));
        return ready || session.state() == QProcess::NotRunning;
    };
    if (!QTest::qWaitFor(named, 5000)) {
        return 0;
    }
    return started.match(log).captured(1).toInt();
}

QByteArray ProxyShutdownTest::outcome(QProcess &session) const
{
    return "status " + QByteArray::number(session.exitStatus()) + " code " + QByteArray::number(session.exitCode()) + "\n"
        + session.readAllStandardError();
}

bool ProxyShutdownTest::socketsLeft() const
{
    return !QDir(qEnvironmentVariable("XDG_RUNTIME_DIR")).entryList({QStringLiteral("upscale-x11-*")}, QDir::Dirs).isEmpty();
}

// SIGTERM, which KWin sends when it stops Xwayland, ends the server and then
// the session, which closes its clients' connections and takes its socket
// directory with it.
void ProxyShutdownTest::terminationEndsServerAndSession()
{
    QProcess session;
    const pid_t server = startSession(session, QStringLiteral("X20"), false);
    QVERIFY(server > 0);
    // A server outliving a failed case would outlive the test as well. Only
    // then: after a case that passed, its PID may belong to another process.
    const auto killServer = qScopeGuard([server]() {
        if (QTest::currentTestFailed()) {
            kill(server, SIGKILL);
        }
    });
    const int client = connectBlocking(m_path);
    QVERIFY(client >= 0);
    const auto closeClient = qScopeGuard([client]() {
        close(client);
    });

    // Taken once and checked: kill() given 0 signals this whole process group.
    const auto pid = static_cast<pid_t>(session.processId());
    QVERIFY(pid > 0);
    QCOMPARE(kill(pid, SIGTERM), 0);
    QVERIFY(session.waitForFinished(5000));
    QVERIFY2(session.exitStatus() == QProcess::NormalExit, outcome(session).constData());
    QVERIFY(gone(server));
    char byte;
    QCOMPARE(read(client, &byte, 1), ssize_t(0));
    QVERIFY(!socketsLeft());
}

// A server that ignores SIGTERM is killed once its grace period has passed,
// and signals that keep arriving meanwhile do not postpone that.
void ProxyShutdownTest::aServerIgnoringTerminationIsKilled()
{
    QProcess session;
    const pid_t server = startSession(session, QStringLiteral("X21"), true);
    QVERIFY(server > 0);
    // A server outliving a failed case would outlive the test as well. Only
    // then: after a case that passed, its PID may belong to another process.
    const auto killServer = qScopeGuard([server]() {
        if (QTest::currentTestFailed()) {
            kill(server, SIGKILL);
        }
    });

    // Taken once and checked: kill() given 0 signals this whole process
    // group, which is what processId() returns once the session has ended.
    const auto pid = static_cast<pid_t>(session.processId());
    QVERIFY(pid > 0);
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer repeat;
    connect(&repeat, &QTimer::timeout, &session, [&session, pid]() {
        if (session.state() != QProcess::NotRunning) {
            kill(pid, SIGTERM);
        }
    });
    kill(pid, SIGTERM);
    repeat.start(200);
    // Waited for with the event loop running, which is what sends the rest.
    const auto ended = [&session]() {
        return session.state() == QProcess::NotRunning;
    };
    QVERIFY(QTest::qWaitFor(ended, 10000));
    repeat.stop();
    const QByteArray report = "the session ended after " + QByteArray::number(elapsed.elapsed()) + " ms, " + outcome(session);
    QVERIFY2(elapsed.elapsed() >= 1400, report.constData());
    QVERIFY(gone(server));
    QVERIFY(!socketsLeft());
}

// Connections opened and closed leave the session holding what it held
// before them. One connection first, so that what a session sets up once is
// in place before counting.
void ProxyShutdownTest::connectionsLeaveNoDescriptorsBehind()
{
    m_path = QFile::encodeName(m_directory.filePath(QStringLiteral("X22")));
    QList<int> kept;
    const auto closeKept = qScopeGuard([&kept]() {
        for (const int descriptor : std::as_const(kept)) {
            close(descriptor);
        }
    });
    const QStringList arguments = listenAt(m_path, kept);
    QVERIFY(!arguments.isEmpty());
    auto session = std::make_unique<UpscaleX11::Session>(QCoreApplication::applicationFilePath());
    QVERIFY(session->start(arguments));
    QVERIFY(disconnectHere(connectHere(m_path)));

    const int before = openDescriptors();
    for (int connection = 0; connection < 500; ++connection) {
        const int client = connectHere(m_path);
        QVERIFY2(client >= 0, qPrintable(QStringLiteral("connection %1").arg(connection)));
        QVERIFY2(disconnectHere(client), qPrintable(QStringLiteral("connection %1").arg(connection)));
    }
    QTRY_COMPARE_WITH_TIMEOUT(openDescriptors(), before, 10000);
}

int main(int argc, char **argv)
{
    // The server a session starts in place of Xwayland, and the session in a
    // process of its own.
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], "-listenfd") == 0) {
            if (qEnvironmentVariableIsSet("UPSCALE_TEST_SERVER_IGNORES_TERM")) {
                std::signal(SIGTERM, SIG_IGN);
                std::fputs("server ignores SIGTERM\n", stderr);
            }
            return serve(std::atoi(argv[index + 1]));
        }
        if (std::strcmp(argv[index], "--session") == 0) {
            const QCoreApplication application(argc, argv);
            return runSession(argv[index + 1]);
        }
    }
    QTemporaryDir directory(QDir::tempPath() + QStringLiteral("/proxy-shutdown-XXXXXX"));
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
    ProxyShutdownTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "x11proxy_shutdown_test.moc"
