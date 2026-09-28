// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "framer.h"
#include <QDebug>
#include <algorithm>
#include <limits>
#include <utility>

namespace UpscaleX11
{

namespace
{
qsizetype padded(qsizetype count)
{
    return (count + 3) & ~qsizetype(3);
}
qsizetype extendedLength(quint32 units, qsizetype header)
{
    if (units > static_cast<quint64>((std::numeric_limits<qsizetype>::max() - header) / 4)) {
        throw std::runtime_error("Protocol frame exceeds protocol limit");
    }
    return header + (static_cast<qsizetype>(units) * 4);
}
}

bool Framer::pending(std::size_t side) const
{
    return !m_buffers.at(side).isEmpty() || m_remaining.at(side) != 0;
}

std::optional<qsizetype> Framer::frameLength(std::size_t side, const QByteArray &bytes)
{
    if (side == 0 && m_setup[0]) {
        if (bytes.isEmpty()) {
            return {};
        }
        const quint8 order = Wire::byte(bytes, 0);
        if (order != 'l' && order != 'B') {
            throw std::runtime_error("Invalid X11 byte order");
        }
        m_wire.little = order == 'l';
        m_orderKnown = true;
        if (bytes.size() < 12) {
            return {};
        }
        return 12 + padded(m_wire.word(bytes, 6)) + padded(m_wire.word(bytes, 8));
    }
    if (!m_orderKnown) {
        throw std::runtime_error("Server data before client setup");
    }
    if (side == 1 && m_setup[1]) {
        return bytes.size() < 8 ? std::nullopt : std::optional<qsizetype>(8 + (m_wire.word(bytes, 6) * 4));
    }
    if (side == 0) {
        return requestLength(bytes);
    }
    if (bytes.size() < 32) {
        return {};
    }
    const quint8 kind = Wire::byte(bytes, 0);
    return kind == 1 || (kind & 127) == 35 ? extendedLength(m_wire.integer(bytes, 4), 32) : 32;
}

std::optional<qsizetype> Framer::requestLength(const QByteArray &bytes) const
{
    if (bytes.size() < 4) {
        return {};
    }
    const quint16 units = m_wire.word(bytes, 2);
    if (units != 0) {
        return qsizetype(units) * 4;
    }
    // Framed exactly as Xwayland frames it (os/io.c, ReadRequestFromClient),
    // because a relay that read the stream differently from the server would
    // inspect and rewrite the wrong bytes. A zero length is a BIG-REQUESTS
    // length only once the client enabled them; before that it is a four-byte
    // request the server answers with BadLength.
    if (!m_bigRequests) {
        return 4;
    }
    if (bytes.size() < 8) {
        return {};
    }
    // The server counts an extended length in units however short of its own
    // eight-byte header it is: 1 is four bytes, and 0 is the header alone,
    // after which the server disconnects the client.
    const quint32 extended = m_wire.integer(bytes, 4);
    return extended == 0 ? 8 : extendedLength(extended, 0);
}

QByteArray Framer::feed(std::size_t side, const QByteArray &bytes, const Handler &handler)
{
    QByteArray &buffer = m_buffers.at(side);
    if (m_transparent) {
        QByteArray output = buffer + bytes;
        buffer.clear();
        return output;
    }
    QByteArray output;
    qsizetype consumed = 0;
    try {
        while (consumed < bytes.size()) {
            const qsizetype count = std::min(bytes.size() - consumed, s_bufferLimit - buffer.size());
            if (!count) {
                throw std::runtime_error("Protocol buffer limit");
            }
            buffer.append(bytes.constData() + consumed, count);
            consumed += count;
            output += frames(side, buffer, handler);
        }
    } catch (const std::exception &error) {
        // The server is trusted to speak the protocol, so a stream from it
        // that cannot be read is a broken connection. A client's bytes are
        // the server's to judge: whatever they are, they are relayed, and the
        // server answers them as it would without this relay in front of it.
        // A connection that would end here instead ends a program that a
        // stock server keeps talking to.
        if (side == 1) {
            throw;
        }
        qInfo() << "Upscale X11 relaying this connection unchanged:" << error.what();
        m_transparent = true;
        output += std::exchange(m_partial, {}) + buffer + bytes.sliced(consumed);
        buffer.clear();
    }
    return output;
}

QByteArray Framer::frames(std::size_t side, QByteArray &buffer, const Handler &handler)
{
    m_partial.clear();
    while (!buffer.isEmpty()) {
        if (m_transparent) {
            // A request just read made the rest of this connection ambiguous.
            m_partial += buffer;
            buffer.clear();
            break;
        }
        if (m_remaining[side] != 0) {
            const qsizetype count = std::min(m_remaining[side], buffer.size());
            m_partial += buffer.first(count);
            buffer.remove(0, count);
            m_remaining[side] -= count;
            continue;
        }
        const std::optional<qsizetype> length = frameLength(side, buffer);
        if (!length) {
            break;
        }
        if (*length < 4) {
            throw std::runtime_error("Invalid protocol frame length");
        }
        if (*length > s_bufferLimit) {
            if (m_setup[side]) {
                throw std::runtime_error("Inspected protocol frame exceeds limit");
            }
            buffer = handler(side, buffer, FrameKind::Stream);
            m_remaining[side] = *length;
            continue;
        }
        if (buffer.size() < *length) {
            break;
        }
        // Removed from the buffer only once it was read, so that a frame that
        // could not be is still there to be relayed as it arrived.
        const FrameKind kind = m_setup[side] ? FrameKind::Setup : FrameKind::Message;
        m_setup[side] = false;
        const QByteArray message = handler(side, buffer.first(*length), kind);
        buffer.remove(0, *length);
        m_partial += message;
    }
    return std::exchange(m_partial, {});
}

} // namespace UpscaleX11
