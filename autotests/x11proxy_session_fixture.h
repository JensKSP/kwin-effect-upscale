// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "wire.h"
#include <QCoreApplication>
#include <QFile>
#include <QSize>
#include <QTest>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

// The whole transport as KWin starts it: listening for clients, asking the
// effect over D-Bus how to answer each one, and relaying to the server it
// started. The server is this program again, answering connection setup the
// way Xwayland does; the effect is a stand-in on the test's own bus.
namespace UpscaleX11Test
{

// The setup a client opens its connection with: little-endian, protocol 11,
// no authorization.
inline QByteArray setupRequest()
{
    const UpscaleX11::Wire wire;
    QByteArray request(12, '\0');
    request[0] = 'l';
    wire.word(request, 2, 11);
    return request;
}

// One screen of 3840x2160, each connection with its own resource range.
inline QByteArray setupReply(const UpscaleX11::Wire &wire, quint32 connection)
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

inline bool readFully(int descriptor, char *data, std::size_t size)
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
inline int serve(int listener)
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

// The address of a socket at @p path, or false where the path does not fit.
inline bool socketAddress(const QByteArray &path, sockaddr_un &address)
{
    address = {};
    address.sun_family = AF_UNIX;
    if (path.isEmpty() || static_cast<std::size_t>(path.size()) >= sizeof(address.sun_path)) {
        return false;
    }
    std::memcpy(address.sun_path, path.constData(), static_cast<std::size_t>(path.size() + 1));
    return true;
}

// The part of a connection setup reply these tests read: up to the first
// screen's size.
using SetupReply = std::array<char, 80>;

// The first screen's size in a setup reply. Read from an array of known size:
// through a QByteArray, GCC 14 at -O2 follows the path on which the array is
// empty into Wire::word() and fails the build with -Warray-bounds, which it
// cannot prove false.
inline QSize rootSize(const SetupReply &reply)
{
    const QByteArray bytes = QByteArray::fromRawData(reply.data(), static_cast<qsizetype>(reply.size()));
    const UpscaleX11::Wire wire;
    return QSize(wire.word(bytes, 60), wire.word(bytes, 62));
}

// A client in another process, which prints the root size it was given. It
// ends its side and reads until the relay has ended the other, which the relay
// does in the same step in which it finishes, so its exit means the relay is
// gone.
inline int connectOnce(const QByteArray &path)
{
    const int client = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    const QByteArray request = setupRequest();
    SetupReply reply{};
    if (client < 0 || !socketAddress(path, address)
        || ::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || write(client, request.constData(), request.size()) != request.size() || !readFully(client, reply.data(), reply.size())) {
        return 1;
    }
    const QSize root = rootSize(reply);
    std::printf("%dx%d\n", root.width(), root.height());
    std::fflush(stdout);
    shutdown(client, SHUT_WR);
    for (;;) {
        char byte;
        const ssize_t count = read(client, &byte, 1);
        if (count == 0) {
            return 0;
        }
        if (count < 0 && errno != EINTR) {
            return 1;
        }
    }
}

// A process as Wine starts one: its first argument is the Windows program it
// runs, and its prefix is in its environment. What it does is this program's.
inline pid_t spawnWine(const QByteArray &program, const QList<QByteArray> &arguments, const QByteArray &prefix)
{
    QList<QByteArray> environment;
    for (char **entry = environ; *entry; ++entry) {
        environment.append(*entry);
    }
    environment.append("WINEPREFIX=" + prefix);
    QList<QByteArray> command{program};
    command += arguments;
    std::vector<char *> argv;
    for (QByteArray &argument : command) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (QByteArray &entry : environment) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);
    const QByteArray self = QFile::encodeName(QCoreApplication::applicationFilePath());
    pid_t pid = -1;
    return posix_spawn(&pid, self.constData(), nullptr, nullptr, argv.data(), envp.data()) == 0 ? pid : -1;
}

// Whether a child ended successfully, waited for without blocking the relay,
// which runs in this thread.
inline bool succeeded(pid_t pid)
{
    int status = 0;
    const auto ended = [pid, &status]() {
        return waitpid(pid, &status, WNOHANG) == pid;
    };
    return pid > 0 && QTest::qWaitFor(ended, 5000) && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace UpscaleX11Test
