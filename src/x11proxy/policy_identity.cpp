// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"

namespace UpscaleX11
{

void Policy::setupReply(QByteArray &bytes)
{
    if (m_registry) {
        m_base = m_wire.integer(bytes, 12);
        m_registration = m_registry->insert(*m_base, m_wire.integer(bytes, 16), m_pid);
    }
    m_setupReply = bytes;
    if (m_display) {
        m_display->setup(bytes);
    }
}

void Policy::resourceReply(QByteArray &bytes)
{
    if (!m_registry) {
        return;
    }
    const quint32 count = m_wire.integer(bytes, 8);
    qsizetype offset = 32;
    for (quint32 index = 0; index < count; ++index) {
        const quint32 resource = m_wire.integer(bytes, offset);
        const quint32 mask = m_wire.integer(bytes, offset + 4);
        // XRes client-ID payload lengths are bytes. Only the outer X11 reply
        // length uses four-byte units; a PID record therefore has length 4.
        const quint32 length = m_wire.integer(bytes, offset + 8);
        Wire::require(bytes, offset, 12);
        if (length > static_cast<quint64>(bytes.size() - offset - 12)) {
            throw std::runtime_error("Truncated XRes client ID");
        }
        if (mask == 2 && length == 4) {
            if (const std::optional<quint32> pid = m_registry->processFor(resource)) {
                m_wire.integer(bytes, offset + 12, *pid);
            }
        }
        offset += 12 + qsizetype(length);
    }
}

bool Policy::streamable(std::size_t side, const QByteArray &bytes) const
{
    if (side == 0) {
        const quint8 major = Wire::byte(bytes, 0);
        return major != 98 && major != 14
            && (m_extensions.value(major) != "RANDR" || Wire::byte(bytes, 1) != 2);
    }
    if (Wire::byte(bytes, 0) != 1) {
        return true;
    }
    const Request request = m_requests.value(m_wire.word(bytes, 2));
    if (request.kind == "X-Resource" && request.operation == 4) {
        return false;
    }
    return !m_display || (request.kind != "geometry" && request.kind != "extension" && request.kind != "RANDR" && request.kind != "XINERAMA" && request.kind != "XFree86-VidModeExtension");
}

} // namespace UpscaleX11
