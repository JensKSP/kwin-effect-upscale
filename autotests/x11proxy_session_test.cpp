// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
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

namespace
{
// What the session logs, for a case that waits until it has said something.
QStringList s_logged;
QtMessageHandler s_passOn = nullptr;

void record(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    s_logged.append(message);
    s_passOn(type, context, message);
}
}

// The effect's side of the question, counting how often it is asked.
class EffectStandIn : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Effect.Upscale1")
public:
    int asked = 0;
    bool prefixMayMatch = true;
    // Programs whose names contain this are not in the list.
    QString unselected;
    QStringList lastCandidates;
public Q_SLOTS:
    bool x11PrefixMayMatch(const QString &prefix, const QStringList &candidates)
    {
        Q_UNUSED(prefix)
        Q_UNUSED(candidates)
        return prefixMayMatch;
    }
    QVariantMap x11ConnectionPolicy(uint pid, const QStringList &candidates)
    {
        Q_UNUSED(pid)
        ++asked;
        lastCandidates = candidates;
        if (!unselected.isEmpty() && candidates.join(QLatin1Char(' ')).contains(unselected)) {
            return {{QStringLiteral("reason"), QStringLiteral("not in the list")}};
        }
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
    void cleanup();
    void oneProcessIsAskedOnce();
    void forgetsWhatAPrefixRanOnceItStops();
    void unselectedWineComponentConnectsPromptly();
    void selectedWineComponentWaitsForProgram();
    void identifiesAProgramStartedByItsUnixPath();
    void aWarmPrefixIsShownTheLaterScreen();
    void effectSwitchedOffMidSession();

private:
    // Starts a session listening at @p name, as KWin starts one. What KWin
    // keeps of the descriptors it hands over stays open until cleanup.
    std::unique_ptr<UpscaleX11::Session> startSession(const QString &name);
    // Opens a connection, sends its setup, and waits for the answer. Returns
    // the descriptor, with the root size the client was told in @p size.
    int connectClient(QSize &size);
    // The root size another program is told, from a process of its own, or
    // "failed" where it could not connect.
    QByteArray screenOfAnotherProgram();
    // Closes a connection once its relay has finished: the relay passes the
    // end of the stream back in the same step in which it finishes.
    bool disconnectClient(int client);

    QTemporaryDir m_directory;
    QByteArray m_path;
    QList<int> m_kept;
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

// Wine's own components come up before the game a prefix was started for and
// are answered for the program the prefix runs. Once every connection of the
// prefix has closed it has stopped, and the next game started in it may be
// another one: a component of that run is answered for that game, not for the
// one before it.
void ProxySessionTest::forgetsWhatAPrefixRanOnceItStops()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X10"));
    QVERIFY(session);
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("prefix")));
    QVERIFY(succeeded(spawnWine("C:\\Games\\First.exe", {"--connect", m_path}, prefix)));
    QVERIFY(m_effect.lastCandidates.join(QLatin1Char(' ')).contains(QStringLiteral("First.exe")));

    const pid_t second = spawnWine("C:\\Games\\Second.exe", {"--wait"}, prefix);
    QVERIFY(second > 0);
    const auto stop = qScopeGuard([second]() {
        kill(second, SIGTERM);
        waitpid(second, nullptr, 0);
    });
    QVERIFY(succeeded(spawnWine("C:\\windows\\system32\\explorer.exe", {"--connect", m_path}, prefix)));
    const QString names = m_effect.lastCandidates.join(QLatin1Char(' '));
    QVERIFY2(names.contains(QStringLiteral("Second.exe")), qPrintable(names));
    QVERIFY2(!names.contains(QStringLiteral("First.exe")), qPrintable(names));
}

void ProxySessionTest::unselectedWineComponentConnectsPromptly()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X11"));
    QVERIFY(session);
    m_effect.prefixMayMatch = false;
    const int before = m_effect.asked;
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("unselected")));
    // No game exists in this prefix. This must finish before the ten-second
    // identity deadline, without requesting an actual display advertisement.
    QVERIFY(succeeded(spawnWine("C:\\windows\\system32\\winecfg.exe", {"--connect", m_path}, prefix)));
    QCOMPARE(m_effect.asked, before);
}

void ProxySessionTest::selectedWineComponentWaitsForProgram()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X12"));
    QVERIFY(session);
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("selected")));
    const pid_t component = spawnWine("C:\\windows\\system32\\explorer.exe", {"--connect", m_path}, prefix);
    QVERIFY(component > 0);
    const int before = m_effect.asked;
    // The session says when it holds a connection for its prefix's program;
    // until then it has not asked anything either.
    s_logged.clear();
    s_passOn = qInstallMessageHandler(record);
    const auto passOn = qScopeGuard([]() {
        qInstallMessageHandler(s_passOn);
    });
    QTRY_VERIFY(s_logged.join(QLatin1Char('\n')).contains(QStringLiteral("waiting for the program of prefix")));
    QCOMPARE(m_effect.asked, before);
    const pid_t game = spawnWine("C:\\Games\\Delayed.exe", {"--wait"}, prefix);
    QVERIFY(game > 0);
    const auto stop = qScopeGuard([game]() {
        kill(game, SIGTERM);
        waitpid(game, nullptr, 0);
    });
    QVERIFY(succeeded(component));
    QCOMPARE(m_effect.asked, before + 1);
    QVERIFY(m_effect.lastCandidates.join(QLatin1Char(' ')).contains(QStringLiteral("Delayed.exe")));
}

// Wine started with a program's Unix path keeps that path in the command line
// rather than a Windows one, and names the program on the drive whose
// directory holds it most closely: Z:, which a prefix maps to /, for a game
// anywhere, and C: for one inside the prefix's own drive. Found 2026-09-29:
// such a program was never identified, and its prefix's connections were
// held for their ten seconds.
void ProxySessionTest::identifiesAProgramStartedByItsUnixPath()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X14"));
    QVERIFY(session);
    const QString prefix = m_directory.filePath(QStringLiteral("unix"));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/dosdevices")));
    QVERIFY(QDir().mkpath(prefix + QStringLiteral("/drive_c/Games")));
    QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("Games"))));
    QVERIFY(QFile::link(QStringLiteral("../drive_c"), prefix + QStringLiteral("/dosdevices/c:")));
    QVERIFY(QFile::link(QStringLiteral("/"), prefix + QStringLiteral("/dosdevices/z:")));
    const QString canonical = QFileInfo(prefix).canonicalFilePath();
    const QString elsewhere = QFileInfo(m_directory.filePath(QStringLiteral("Games"))).canonicalFilePath();
    QVERIFY(succeeded(spawnWine(QFile::encodeName(elsewhere + QStringLiteral("/Elsewhere.exe")), {"--connect", m_path}, QFile::encodeName(canonical))));
    QVERIFY2(m_effect.lastCandidates.contains(QStringLiteral("wine://") + canonical + QStringLiteral("/Z:") + elsewhere + QStringLiteral("/Elsewhere.exe")),
             qPrintable(m_effect.lastCandidates.join(QLatin1Char(' '))));
    QVERIFY(succeeded(spawnWine(QFile::encodeName(canonical + QStringLiteral("/drive_c/Games/Inside.exe")), {"--connect", m_path}, QFile::encodeName(canonical))));
    QVERIFY2(m_effect.lastCandidates.contains(QStringLiteral("wine://") + canonical + QStringLiteral("/C:/Games/Inside.exe")),
             qPrintable(m_effect.lastCandidates.join(QLatin1Char(' '))));
}

// A prefix that runs already when a selected program starts in it - a launcher
// first, or Wine's own tools - has its earlier connections shown that
// program's screen as well, those a process opened after its first included,
// and no other prefix's. Found 2026-09-29: the program was answered while
// Wine already knew the screen at full size.
void ProxySessionTest::aWarmPrefixIsShownTheLaterScreen()
{
#if !defined(Q_OS_LINUX)
    QSKIP("a Wine process is identified only where another process's command line can be read");
#endif
    const auto session = startSession(QStringLiteral("X15"));
    QVERIFY(session);
    m_effect.unselected = QStringLiteral("Launcher.exe");
    s_logged.clear();
    s_passOn = qInstallMessageHandler(record);
    const auto passOn = qScopeGuard([]() {
        qInstallMessageHandler(s_passOn);
    });
    const QByteArray prefix = QFile::encodeName(m_directory.filePath(QStringLiteral("warm")));
    const pid_t launcher = spawnWine("C:\\Games\\Launcher.exe", {"--hold", m_path}, prefix);
    const pid_t elsewhere = spawnWine("C:\\Games\\Launcher.exe", {"--hold", m_path}, QFile::encodeName(m_directory.filePath(QStringLiteral("cold"))));
    QVERIFY(launcher > 0 && elsewhere > 0);
    const auto stop = qScopeGuard([launcher, elsewhere]() {
        for (const pid_t pid : {launcher, elsewhere}) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    });
    // Both launchers answered, at the size they have, before the game starts.
    QTRY_COMPARE(s_logged.filter(QStringLiteral("not in the list")).size(), 2);
    QVERIFY(succeeded(spawnWine("C:\\Games\\Game.exe", {"--connect", m_path}, prefix)));
    const QString log = s_logged.join(QLatin1Char('\n'));
    QVERIFY2(log.contains(QStringLiteral("now shows 2 earlier connections QSize(2560, 1440)")), qPrintable(log));
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
#include "x11proxy_session_test.moc"
