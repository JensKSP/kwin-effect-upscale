// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "wire.h"
#include <array>
#include <functional>
#include <optional>

namespace UpscaleX11
{

// Owns both directions' framing state. The handler runs before the next frame
// is read, so a request enabling BIG-REQUESTS changes the very next boundary.
// It may rewrite a complete message, reject an inspected oversized frame, or
// stop interpreting an ambiguous connection. Buffered bytes remain available
// for unchanged forwarding if interpreting a client frame fails.
class Framer
{
public:
    static constexpr qsizetype s_bufferLimit = qsizetype(8) * 1024 * 1024;
    enum class FrameKind {
        Setup,
        Message,
        Stream,
    };
    using Handler = std::function<QByteArray(std::size_t, QByteArray, FrameKind)>;

    explicit Framer(Wire &wire)
        : m_wire(wire)
    {
    }
    QByteArray feed(std::size_t side, const QByteArray &bytes, const Handler &handler);
    bool pending(std::size_t side) const;
    bool transparent() const
    {
        return m_transparent;
    }
    void relayUnchanged()
    {
        m_transparent = true;
    }
    bool bigRequestsEnabled() const
    {
        return m_bigRequests;
    }
    void enableBigRequests()
    {
        m_bigRequests = true;
    }

private:
    std::optional<qsizetype> frameLength(std::size_t side, const QByteArray &bytes);
    std::optional<qsizetype> requestLength(const QByteArray &bytes) const;
    QByteArray frames(std::size_t side, QByteArray &buffer, const Handler &handler);
    Wire &m_wire;
    std::array<qsizetype, 2> m_remaining{};
    std::array<QByteArray, 2> m_buffers;
    QByteArray m_partial;
    std::array<bool, 2> m_setup{true, true};
    bool m_orderKnown = false;
    // Set only after a well-formed BigReqEnable, as on the server.
    bool m_bigRequests = false;
    // Once request framing becomes ambiguous, both streams pass unchanged.
    bool m_transparent = false;
};

} // namespace UpscaleX11
