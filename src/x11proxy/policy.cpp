// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"
#include <utility>

namespace UpscaleX11
{

namespace
{
qsizetype padded(qsizetype count)
{
    return (count + 3) & ~qsizetype(3);
}
}

Policy::Policy(QSize size, Registry *registry, quint32 pid, const QByteArray &timing)
    : m_registry(registry)
    , m_pid(pid)
{
    if (size.isValid()) {
        m_display = std::make_unique<DisplayReplies>(m_wire, size, timing);
    }
}

Policy::~Policy()
{
    if (m_registry && m_base) {
        m_registry->remove(*m_base, m_registration);
    }
}

QByteArray Policy::feed(std::size_t side, const QByteArray &bytes)
{
    return m_framer.feed(side, bytes, [this](std::size_t direction, QByteArray message, Framer::FrameKind kind) {
        return frame(direction, std::move(message), kind);
    });
}

QByteArray Policy::frame(std::size_t side, QByteArray message, Framer::FrameKind kind)
{
    if (kind == Framer::FrameKind::Stream) {
        if (!streamable(side, message)) {
            throw std::runtime_error("Inspected protocol frame exceeds limit");
        }
        if (side == 0) {
            request(message);
        } else if (Wire::byte(message, 0) == 1) {
            m_requests.remove(m_wire.word(message, 2));
        }
    } else if (kind == Framer::FrameKind::Setup) {
        if (side == 1 && Wire::byte(message, 0) == 1) {
            setupReply(message);
        }
    } else if (side == 0) {
        request(message);
    } else {
        message = response(std::move(message));
    }
    return message;
}

void Policy::request(QByteArray &bytes)
{
    const auto sequence = static_cast<quint16>(++m_sequence);
    m_requests.remove(sequence);
    const quint8 major = Wire::byte(bytes, 0);
    const quint8 minor = Wire::byte(bytes, 1);
    const qsizetype shift = bytes.size() >= 8 && m_wire.word(bytes, 2) == 0 ? 4 : 0;
    // A request is read only when it has exactly the size the server accepts
    // for it. Any other is the server's to refuse with BadLength, and is
    // relayed as it came, with nothing read from beyond its end.
    if (major == 98) {
        if (bytes.size() >= 8 + shift) {
            const quint16 length = m_wire.word(bytes, 4 + shift);
            if (bytes.size() == 8 + shift + padded(length)) {
                m_requests.insert(sequence, {"extension", 0, bytes.mid(8 + shift, length)});
            }
        }
    } else if (major == 14) {
        if (bytes.size() == 8 + shift) {
            m_requests.insert(sequence, {"geometry", m_wire.integer(bytes, 4 + shift), {}});
        }
    } else if (m_extensions.contains(major)) {
        const QByteArray extension = m_extensions.value(major);
        m_requests.insert(sequence, {extension, minor, {}});
        // Enabled where the server enables it (Xext/bigreq.c): BigReqEnable,
        // one unit long, and nothing else.
        if (extension == "BIG-REQUESTS" && minor == 0 && bytes.size() == 4) {
            m_framer.enableBigRequests();
        }
        if (m_display && extension == "RANDR") {
            m_display->request(minor, bytes, shift);
        }
    } else if (major >= 128 && minor == 0 && bytes.size() == 4 && !m_framer.bigRequestsEnabled()) {
        // Shaped like BigReqEnable, on an opcode this connection never asked
        // the server about. Were it that, the server would read the next zero
        // length otherwise than this relay could know, so from here on this
        // connection is relayed without being read.
        m_framer.relayUnchanged();
    }
}

QByteArray Policy::response(QByteArray bytes)
{
    const quint8 kind = Wire::byte(bytes, 0);
    if (kind != 0 && kind != 1) {
        if (m_display) {
            m_display->event(bytes);
        }
        return bytes;
    }
    const Request request = m_requests.take(m_wire.word(bytes, 2));
    if (kind == 0 || request.kind.isEmpty()) {
        return bytes;
    }
    if (request.kind == "extension") {
        if (Wire::byte(bytes, 8)) {
            m_extensions.insert(Wire::byte(bytes, 9), request.extension);
            if (m_display && request.extension == "RANDR") {
                m_display->randrEvent = Wire::byte(bytes, 10);
            }
        }
        return bytes;
    }
    if (request.kind == "X-Resource" && request.operation == 4) {
        resourceReply(bytes);
    }
    if (m_display) {
        bytes = m_display->reply(request.kind, request.operation, std::move(bytes));
    }
    bytes.append(QByteArray(padded(bytes.size()) - bytes.size(), '\0'));
    m_wire.integer(bytes, 4, static_cast<quint32>((bytes.size() - 32) / 4));
    return bytes;
}

} // namespace UpscaleX11
