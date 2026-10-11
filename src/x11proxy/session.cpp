// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "session.h"
#include "lifecycle.h"
#include "relay.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace UpscaleX11
{
namespace
{
volatile std::sig_atomic_t signalWriteDescriptor = -1;
void terminateHandler(int signalNumber)
{
    const int savedError = errno;
    const char value = static_cast<char>(signalNumber);
    const ssize_t ignored = write(signalWriteDescriptor, &value, 1);
    (void)ignored;
    errno = savedError;
}
}
Session::Session(const QString &program)
    : m_directory(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/upscale-x11-XXXXXX"))
{
    m_server.setProgram(program);
    m_server.setProcessChannelMode(QProcess::ForwardedChannels);
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, &m_server, &QProcess::kill);
    connect(&m_server, &QProcess::started, this, [this]() {
        qInfo() << "Upscale X11 backend started pid=" << m_server.processId() << "uid=" << getuid();
        for (const int descriptor : std::as_const(m_startup.childDescriptors)) {
            close(descriptor);
        }
        m_startup.childDescriptors.clear();
        m_backendListener = -1;
    });
    connect(&m_server, &QProcess::finished, this, [](int code, QProcess::ExitStatus status) {
        qInfo() << "Upscale X11 backend exited code=" << code << "status=" << status;
        QCoreApplication::exit(status == QProcess::NormalExit ? code : 1);
    });
    connect(&m_server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qWarning() << "Upscale X11 backend error:" << m_server.errorString();
        if (error == QProcess::FailedToStart) {
            QCoreApplication::exit(127);
        }
    });
}
Session::~Session()
{
    signalWriteDescriptor = -1;
    // Relays remove their resource IDs from the registry during destruction.
    // Destroy them before member teardown rather than in QObject's destructor.
    const QObjectList owned = children();
    qDeleteAll(owned);
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        if (!m_server.waitForFinished(1500)) {
            m_server.kill();
            m_server.waitForFinished(1500);
        }
    }
    for (const int descriptor : std::as_const(m_signalSockets)) {
        close(descriptor);
    }
    for (const int descriptor : std::as_const(m_startup.listeners)) {
        close(descriptor);
    }
    for (const int descriptor : std::as_const(m_startup.childDescriptors)) {
        close(descriptor);
    }
    if (m_backendListener >= 0 && !m_startup.childDescriptors.contains(m_backendListener)) {
        close(m_backendListener);
    }
}
bool Session::listen()
{
    if (!m_directory.isValid()) {
        return startupFailed(QStringLiteral("runtime directory %1").arg(m_directory.path()), m_directory.errorString());
    }
    m_backendPath = QFile::encodeName(m_directory.path() + QStringLiteral("/x11"));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    // A local socket's address holds its path, and only so many bytes of it.
    // The runtime directory makes that path, and a desktop's is short; a
    // directory deep in a build tree is not.
    if (m_backendPath.size() >= static_cast<qsizetype>(sizeof(address.sun_path))) {
        return startupFailed(QStringLiteral("backend socket %1").arg(QFile::decodeName(m_backendPath)),
                             QStringLiteral("longer than the %1 bytes a socket address holds").arg(static_cast<int>(sizeof(address.sun_path) - 1)));
    }
    std::memcpy(address.sun_path, m_backendPath.constData(), static_cast<std::size_t>(m_backendPath.size() + 1));
    m_backendListener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_backendListener < 0 || !socketFlags(m_backendListener)) {
        return startupFailed(QStringLiteral("backend socket"), systemError(errno));
    }
    if (bind(m_backendListener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        return startupFailed(QStringLiteral("binding %1").arg(QFile::decodeName(m_backendPath)), systemError(errno));
    }
    if (::listen(m_backendListener, 128) != 0) {
        return startupFailed(QStringLiteral("listening on %1").arg(QFile::decodeName(m_backendPath)), systemError(errno));
    }
    return true;
}
bool Session::watchSignals()
{
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        return startupFailed(QStringLiteral("signal socket pair"), systemError(errno));
    }
    m_signalSockets = {pair[0], pair[1]};
    if (!socketFlags(pair[0]) || !socketFlags(pair[1])) {
        return startupFailed(QStringLiteral("signal socket pair"), systemError(errno));
    }
    signalWriteDescriptor = pair[1];
    std::signal(SIGTERM, terminateHandler);
    std::signal(SIGINT, terminateHandler);
    auto *notifier = new QSocketNotifier(pair[0], QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this]() {
        char values[64];
        const ssize_t ignored = read(m_signalSockets[0], values, sizeof(values));
        (void)ignored;
        // The first signal starts the grace period and later ones leave it
        // alone: restarting it would let signals that keep arriving postpone
        // the kill of a server that ignores them for as long as they do.
        if (!m_killTimer.isActive()) {
            m_server.terminate();
            m_killTimer.start(1500);
        }
    });
    return true;
}
bool Session::start(const QStringList &arguments)
{
    if (!listen() || !m_startup.parse(arguments, m_backendListener) || !watchSignals()) {
        return false;
    }
    // KWin itself asks XRes for client PIDs. Keep its WM channel in the
    // identity relay so that it sees original peer PIDs, never the proxy PID.
    new Relay(m_startup.windowManager, m_startup.windowManagerBackend, this, {.size = {}, .timing = {}, .registry = &m_registry, .pid = static_cast<quint32>(getppid())});
    for (const int listener : std::as_const(m_startup.listeners)) {
        if (!socketFlags(listener)) {
            return startupFailed(QStringLiteral("-listenfd %1").arg(listener), systemError(errno));
        }
        auto *notifier = new QSocketNotifier(listener, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this, listener]() {
            acceptClient(listener);
        });
    }
    m_server.setArguments(m_startup.arguments);
    m_server.setChildProcessModifier([this]() {
        for (const int descriptor : std::as_const(m_startup.listeners)) {
            close(descriptor);
        }
        for (const int descriptor : std::as_const(m_startup.childDescriptors)) {
            const int flags = fcntl(descriptor, F_GETFD);
            if (flags < 0 || fcntl(descriptor, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
                m_server.failChildProcessModifier("preserve Xwayland descriptor", errno);
            }
        }
    });
    new Lifecycle(this);
    m_server.start();
    return true;
}
}
