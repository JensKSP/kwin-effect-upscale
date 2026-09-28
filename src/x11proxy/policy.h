// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "display.h"
#include "framer.h"
#include "registry.h"
#include <memory>
#include <optional>

namespace UpscaleX11
{

class Policy
{
public:
    // The largest frame held whole to be read. Larger ones are relayed as
    // they arrive. Coalesced frames are drained before buffering more input.
    static constexpr qsizetype s_bufferLimit = Framer::s_bufferLimit;
    explicit Policy(QSize size = {}, Registry *registry = nullptr, quint32 pid = 0, const QByteArray &timing = {});
    ~Policy();
    QByteArray feed(std::size_t side, const QByteArray &bytes);
    bool transparent() const
    {
        return m_framer.transparent();
    }
    bool pending(std::size_t side) const
    {
        return m_framer.pending(side);
    }

private:
    struct Request
    {
        QByteArray kind;
        quint32 operation = 0;
        QByteArray extension;
    };
    QByteArray frame(std::size_t side, QByteArray message, Framer::FrameKind kind);
    void request(QByteArray &bytes);
    QByteArray response(QByteArray bytes);
    void setupReply(QByteArray &bytes);
    void resourceReply(QByteArray &bytes);
    bool streamable(std::size_t side, const QByteArray &bytes) const;
    Wire m_wire;
    Framer m_framer{m_wire};
    std::unique_ptr<DisplayReplies> m_display;
    Registry *m_registry;
    quint32 m_pid;
    std::optional<quint32> m_base;
    quint64 m_registration = 0;
    quint64 m_sequence = 0;
    QHash<quint16, Request> m_requests;
    QHash<quint8, QByteArray> m_extensions;
};

} // namespace UpscaleX11
