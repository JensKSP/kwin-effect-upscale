// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "startup.h"
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDebug>
#include <QFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <vector>
namespace UpscaleX11
{
bool requestedRouting()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    return KConfigGroup(config, QStringLiteral("Plugins")).readEntry("upscaleEnabled", true)
        && KConfigGroup(config, QStringLiteral("Effect-upscale")).readEntry("X11Proxy", true);
}
bool socketFlags(int descriptor, bool nonblocking)
{
    const int flags = fcntl(descriptor, F_GETFL);
    return flags >= 0 && fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0
        && (!nonblocking || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0);
}
int startDirect(const QString &program, const QStringList &arguments)
{
    QList<QByteArray> strings{QFile::encodeName(program)};
    for (const QString &argument : arguments) {
        strings.push_back(argument.toLocal8Bit());
    }
    std::vector<char *> pointers;
    for (QByteArray &string : strings) {
        pointers.push_back(string.data());
    }
    pointers.push_back(nullptr);
    execv(pointers.front(), pointers.data());
    qCritical() << "Cannot execute stock Xwayland:" << QString::fromStdString(std::error_code(errno, std::generic_category()).message());
    return 127;
}
quint32 peerProcess(int descriptor)
{
    // Qt and POSIX expose no peer PID API. Use the platform's authenticated
    // local-socket credentials, never an application-supplied window property.
#if defined(Q_OS_LINUX)
    struct ucred credentials = {};
    socklen_t length = sizeof(credentials);
    if (getsockopt(descriptor, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0
        && length == sizeof(credentials) && credentials.uid == getuid() && credentials.pid > 0) {
        return static_cast<quint32>(credentials.pid);
    }
#elif defined(LOCAL_PEERPID)
    uid_t user = 0;
    gid_t group = 0;
    pid_t process = 0;
    socklen_t length = sizeof(process);
    if (getpeereid(descriptor, &user, &group) == 0 && user == getuid()
        && getsockopt(descriptor, SOL_LOCAL, LOCAL_PEERPID, &process, &length) == 0
        && length == sizeof(process) && process > 0) {
        return static_cast<quint32>(process);
    }
#else
    Q_UNUSED(descriptor)
#endif
    return 0;
}
bool Startup::descriptorArgument(const QString &argument, int descriptor, int backendListener)
{
    if (argument == QLatin1String("-listenfd")) {
        listeners.append(descriptor);
        if (listeners.size() == 1) {
            arguments << argument << QString::number(backendListener);
        }
    } else if (argument == QLatin1String("-wm")) {
        if (windowManager >= 0) {
            return false;
        }
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
            return false;
        }
        windowManager = descriptor;
        windowManagerBackend = pair[0];
        childDescriptors.append(pair[1]);
        arguments << argument << QString::number(pair[1]);
    } else {
        childDescriptors.append(descriptor);
        arguments << argument << QString::number(descriptor);
    }
    return true;
}
bool Startup::parse(const QStringList &original, int backendListener)
{
    childDescriptors.append(backendListener);
    for (qsizetype index = 0; index < original.size(); ++index) {
        const QString &argument = original[index];
        if (argument != QLatin1String("-listenfd") && argument != QLatin1String("-wm")
            && argument != QLatin1String("-displayfd") && argument != QLatin1String("-initfd")) {
            arguments.append(argument);
            continue;
        }
        if (++index >= original.size()) {
            return false;
        }
        bool valid = false;
        const int descriptor = original[index].toInt(&valid);
        if (!valid || descriptor < 0 || fcntl(descriptor, F_GETFD) < 0) {
            return false;
        }
        if (!descriptorArgument(argument, descriptor, backendListener)) {
            return false;
        }
    }
    bool valid = false;
    const int wayland = qEnvironmentVariableIntValue("WAYLAND_SOCKET", &valid);
    if (!valid || wayland < 0 || fcntl(wayland, F_GETFD) < 0 || listeners.isEmpty() || windowManager < 0) {
        return false;
    }
    childDescriptors.append(wayland);
    // Do not leave an orphaned backend resetting itself after proxy failure.
    if (!arguments.contains(QStringLiteral("-terminate"))) {
        arguments.append(QStringLiteral("-terminate"));
    }
    return true;
}
}
