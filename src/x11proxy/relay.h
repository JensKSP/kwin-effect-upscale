// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QSocketNotifier>
#include <array>
#include <deque>
#include <memory>
#include <vector>

namespace UpscaleX11
{

class Policy;
class Registry;
struct ConnectionPolicy
{
    QSize size;
    QByteArray timing;
    Registry *registry = nullptr;
    quint32 pid = 0;
};
class Relay : public QObject
{
public:
    Relay(int client, int backend, QObject *parent, const ConnectionPolicy &policy = {});
    ~Relay() override;
    /**
     * Show the client a screen of @p size from now on, and tell it so; see
     * Policy::changeDisplay(). Whether it saw another screen until now.
     */
    bool changeDisplay(const QSize &size, const QByteArray &timing);
    /** The process the connection belongs to, or 0 where it is not known. */
    quint32 pid() const
    {
        return m_pid;
    }

private:
    struct Packet
    {
        QByteArray bytes;
        std::vector<int> descriptors;
        qsizetype offset = 0;
    };
    struct Endpoint
    {
        int socket = -1;
        std::unique_ptr<QSocketNotifier> reader;
        std::unique_ptr<QSocketNotifier> writer;
        std::deque<Packet> queue;
        std::vector<int> pendingDescriptors;
        qsizetype queuedBytes = 0;
        qsizetype queuedDescriptors = 0;
        bool eof = false;
        bool shutdown = false;
    };
    void receive(std::size_t side);
    void queuePacket(std::size_t side, Packet packet);
    void deliver(std::size_t side, Packet packet);
    void send(std::size_t side);
    void refresh();
    void finish(const char *reason);
    std::array<Endpoint, 2> m_endpoints;
    std::unique_ptr<Policy> m_policy;
    quint32 m_pid = 0;
    bool m_finished = false;
    quint64 m_bytes = 0;
    quint64 m_receivedDescriptors = 0;
    quint64 m_sentDescriptors = 0;
};

} // namespace UpscaleX11
