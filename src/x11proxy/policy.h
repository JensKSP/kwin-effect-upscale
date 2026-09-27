// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "display.h"
#include "registry.h"
#include <array>
#include <memory>
#include <optional>

namespace UpscaleX11
{

class Policy
{
public:
    // The largest frame held whole to be read. Larger ones are relayed as
    // they arrive, so this is also the most a single read can release.
    static constexpr qsizetype s_bufferLimit = qsizetype(8) * 1024 * 1024;
    explicit Policy(QSize size = {}, Registry *registry = nullptr, quint32 pid = 0, const QByteArray &timing = {});
    ~Policy();
    QByteArray feed(std::size_t side, const QByteArray &bytes);
    bool transparent() const
    {
        return m_transparent;
    }
    bool pending(std::size_t side) const;

private:
    struct Request
    {
        QByteArray kind;
        quint32 operation = 0;
        QByteArray extension;
    };
    std::optional<qsizetype> frameLength(std::size_t side, const QByteArray &bytes);
    std::optional<qsizetype> requestLength(const QByteArray &bytes) const;
    void beginStreaming(std::size_t side, QByteArray &buffer, qsizetype length);
    QByteArray frames(std::size_t side, QByteArray &buffer);
    QByteArray frame(std::size_t side, QByteArray message);
    void request(QByteArray &bytes);
    QByteArray response(QByteArray bytes);
    void setupReply(QByteArray &bytes);
    void resourceReply(QByteArray &bytes);
    bool streamable(std::size_t side, const QByteArray &bytes) const;
    Wire m_wire;
    std::unique_ptr<DisplayReplies> m_display;
    Registry *m_registry;
    quint32 m_pid;
    std::optional<quint32> m_base;
    quint64 m_registration = 0;
    std::array<qsizetype, 2> m_remaining{};
    std::array<QByteArray, 2> m_buffers;
    QByteArray m_partial;
    std::array<bool, 2> m_setup{true, true};
    bool m_orderKnown = false;
    // Whether the server now reads a zero length as a BIG-REQUESTS length. It
    // does only after a well-formed BigReqEnable, so framing follows it.
    bool m_bigRequests = false;
    // Set once this connection's requests can no longer be framed the way the
    // server frames them. Everything is then relayed as it arrives.
    bool m_transparent = false;
    quint64 m_sequence = 0;
    QHash<quint16, Request> m_requests;
    QHash<quint8, QByteArray> m_extensions;
};

} // namespace UpscaleX11
