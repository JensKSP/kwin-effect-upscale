// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "relay.h"
#include "policy.h"

#include <QDebug>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace UpscaleX11
{

namespace
{
constexpr qsizetype readSize = 65536;
// The policy relays a frame it reads only once all of it arrived, so one read
// can release a whole buffered frame and the read itself at once. A side is
// read only while the other's queue has room for that, which keeps a valid
// frame from ever reaching the limit: before this a request of a few MiB,
// sent while the server was slow to read, ended its connection.
constexpr qsizetype burstLimit = Policy::s_bufferLimit + readSize;
constexpr qsizetype queueLimit = 2 * Policy::s_bufferLimit;
constexpr qsizetype descriptorLimit = 256;
void closeDescriptors(std::vector<int> &descriptors)
{
    for (const int descriptor : descriptors) {
        close(descriptor);
    }
    descriptors.clear();
}
bool temporarilyUnavailable()
{
    return errno == EAGAIN || errno == EINTR
#if EWOULDBLOCK != EAGAIN
        || errno == EWOULDBLOCK
#endif
        ;
}
bool receiveDescriptors(msghdr &message, std::vector<int> &descriptors)
{
    bool invalid = (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) != 0;
    for (cmsghdr *header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) {
            invalid = true;
            continue;
        }
        const std::size_t length = header->cmsg_len - CMSG_LEN(0);
        for (std::size_t offset = 0; offset + sizeof(int) <= length; offset += sizeof(int)) {
            int descriptor = -1;
            std::memcpy(&descriptor, CMSG_DATA(header) + offset, sizeof(descriptor));
            descriptors.push_back(descriptor);
            if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
                invalid = true;
            }
        }
    }
    return !invalid;
}

}

Relay::Relay(int client, int backend, QObject *parent, const ConnectionPolicy &policy)
    : QObject(parent)
    , m_pid(policy.pid)
{
    m_endpoints[0].socket = client;
    m_endpoints[1].socket = backend;
    if (policy.size.isValid() || policy.registry) {
        m_policy = std::make_unique<Policy>(policy.size, policy.registry, policy.pid, policy.timing);
    }
    for (std::size_t side = 0; side < m_endpoints.size(); ++side) {
        Endpoint &endpoint = m_endpoints[side];
        const int flags = fcntl(endpoint.socket, F_GETFL);
        if (flags < 0 || fcntl(endpoint.socket, F_SETFL, flags | O_NONBLOCK) < 0
            || fcntl(endpoint.socket, F_SETFD, FD_CLOEXEC) < 0) {
            finish("socket flags");
            return;
        }
        endpoint.reader = std::make_unique<QSocketNotifier>(endpoint.socket, QSocketNotifier::Read);
        endpoint.writer = std::make_unique<QSocketNotifier>(endpoint.socket, QSocketNotifier::Write);
        endpoint.writer->setEnabled(false);
        connect(endpoint.reader.get(), &QSocketNotifier::activated, this, [this, side]() {
            receive(side);
        });
        connect(endpoint.writer.get(), &QSocketNotifier::activated, this, [this, side]() {
            send(side);
        });
    }
    qInfo() << "RELAY OPEN";
}

Relay::~Relay()
{
    for (Endpoint &endpoint : m_endpoints) {
        endpoint.reader.reset();
        endpoint.writer.reset();
        if (endpoint.socket >= 0) {
            close(endpoint.socket);
        }
        for (Packet &packet : endpoint.queue) {
            closeDescriptors(packet.descriptors);
        }
        closeDescriptors(endpoint.pendingDescriptors);
    }
}

void Relay::receive(std::size_t side)
{
    if (m_finished) {
        return;
    }
    Endpoint &source = m_endpoints[side];
    const Endpoint &target = m_endpoints[1 - side];
    Packet packet;
    packet.bytes.resize(readSize);
    alignas(cmsghdr) std::array<unsigned char, CMSG_SPACE(descriptorLimit * sizeof(int))> control{};
    iovec buffer{packet.bytes.data(), static_cast<std::size_t>(packet.bytes.size())};
    msghdr message{};
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    const ssize_t count = recvmsg(source.socket, &message, 0);
    if (count < 0) {
        if (!temporarilyUnavailable()) {
            finish("recvmsg");
        }
        return;
    }
    const bool invalid = !receiveDescriptors(message, packet.descriptors);
    m_receivedDescriptors += packet.descriptors.size();
    if (invalid || target.queuedDescriptors + static_cast<qsizetype>(source.pendingDescriptors.size() + packet.descriptors.size()) > descriptorLimit) {
        closeDescriptors(packet.descriptors);
        finish("ancillary data limit/error");
        return;
    }
    if (count == 0) {
        closeDescriptors(packet.descriptors);
        if (m_policy && (m_policy->pending(side) || !source.pendingDescriptors.empty())) {
            finish("eof inside a protocol frame");
            return;
        }
        source.eof = true;
    } else {
        m_bytes += static_cast<quint64>(count);
        packet.bytes.resize(count);
        queuePacket(side, std::move(packet));
        return;
    }
    refresh();
}

void Relay::queuePacket(std::size_t side, Packet packet)
{
    Endpoint &source = m_endpoints[side];
    if (m_policy) {
        try {
            packet.bytes = m_policy->feed(side, packet.bytes);
        } catch (const std::exception &error) {
            closeDescriptors(packet.descriptors);
            finish(error.what());
            return;
        }
        source.pendingDescriptors.insert(source.pendingDescriptors.end(), packet.descriptors.begin(), packet.descriptors.end());
        packet.descriptors.clear();
        if (packet.bytes.isEmpty()) {
            return;
        }
        // X11 extension descriptors are consumed in protocol order. Keep
        // their FIFO ordering while fragmented frames await completion.
        packet.descriptors.swap(source.pendingDescriptors);
    }
    deliver(side, std::move(packet));
}

bool Relay::changeDisplay(const QSize &size, const QByteArray &timing)
{
    if (!m_policy || m_finished || !m_policy->changesDisplay(size)) {
        return false;
    }
    Packet events;
    events.bytes = m_policy->changeDisplay(size, timing);
    qInfo() << "Upscale X11 connection pid=" << m_pid << "shown" << size << "told with" << events.bytes.size() / 32 << "events";
    if (!events.bytes.isEmpty()) {
        // To the client, as though the server had sent them.
        deliver(1, std::move(events));
    }
    return true;
}

// Queues what arrived from @p side for the other end, and writes it at once
// where nothing waits before it.
void Relay::deliver(std::size_t side, Packet packet)
{
    Endpoint &target = m_endpoints[1 - side];
    if (packet.bytes.size() > queueLimit - target.queuedBytes) {
        closeDescriptors(packet.descriptors);
        finish("output queue limit");
        return;
    }
    target.queuedBytes += packet.bytes.size();
    target.queuedDescriptors += static_cast<qsizetype>(packet.descriptors.size());
    const bool waiting = !target.queue.empty();
    target.queue.push_back(std::move(packet));
    if (!waiting) {
        // Written at once, not on the event loop's next pass: that pass is
        // added to every round trip, the window manager's included. Behind
        // the relay KWin finished managing a new window about a millisecond
        // later than without it (measured 2026-09-27), late enough for the X
        // Test Suite's event cases to see it act after they began.
        send(1 - side);
        return;
    }
    refresh();
}

void Relay::send(std::size_t side)
{
    if (m_finished) {
        return;
    }
    Endpoint &target = m_endpoints[side];
    Packet &packet = target.queue.front();
    iovec buffer{packet.bytes.data() + packet.offset, static_cast<std::size_t>(packet.bytes.size() - packet.offset)};
    alignas(cmsghdr) std::array<unsigned char, CMSG_SPACE(descriptorLimit * sizeof(int))> control{};
    msghdr message{};
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    if (!packet.descriptors.empty()) {
        message.msg_control = control.data();
        message.msg_controllen = CMSG_SPACE(packet.descriptors.size() * sizeof(int));
        cmsghdr *header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(packet.descriptors.size() * sizeof(int));
        std::memcpy(CMSG_DATA(header), packet.descriptors.data(), packet.descriptors.size() * sizeof(int));
    }
    const ssize_t count = sendmsg(target.socket, &message, 0);
    if (count < 0) {
        if (temporarilyUnavailable()) {
            refresh();
        } else {
            finish("sendmsg");
        }
        return;
    }
    if (count == 0) {
        finish("sendmsg made no progress");
        return;
    }
    // Rights travel once, even when the stream accepts only part of the bytes.
    m_sentDescriptors += packet.descriptors.size();
    target.queuedDescriptors -= static_cast<qsizetype>(packet.descriptors.size());
    closeDescriptors(packet.descriptors);
    packet.offset += count;
    target.queuedBytes -= count;
    if (packet.offset == packet.bytes.size()) {
        target.queue.pop_front();
    }
    refresh();
}

void Relay::refresh()
{
    bool drained = true;
    for (std::size_t side = 0; side < m_endpoints.size(); ++side) {
        Endpoint &endpoint = m_endpoints[side];
        const Endpoint &other = m_endpoints[1 - side];
        if (other.eof && endpoint.queue.empty() && !endpoint.shutdown) {
            shutdown(endpoint.socket, SHUT_WR);
            endpoint.shutdown = true;
        }
        endpoint.reader->setEnabled(!endpoint.eof && other.queuedBytes <= queueLimit - burstLimit);
        endpoint.writer->setEnabled(!endpoint.queue.empty());
        drained = drained && endpoint.eof && endpoint.queue.empty();
    }
    if (drained) {
        finish("eof");
    }
}

void Relay::finish(const char *reason)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    for (Endpoint &endpoint : m_endpoints) {
        if (endpoint.reader) {
            endpoint.reader->setEnabled(false);
            endpoint.writer->setEnabled(false);
        }
    }
    qInfo() << "RELAY CLOSE bytes=" << m_bytes << "received-fds=" << m_receivedDescriptors
            << "sent-fds=" << m_sentDescriptors << "reason=" << reason;
    deleteLater();
}

} // namespace UpscaleX11
