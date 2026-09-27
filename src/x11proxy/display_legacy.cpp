// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "display.h"

namespace UpscaleX11
{

QByteArray DisplayReplies::screenInfo(const QByteArray &bytes)
{
    const quint16 count = m_wire.word(bytes, 20);
    qsizetype rates = 32 + (count * 8);
    quint16 current = 0;
    quint16 kept = 0;
    bool found = false;
    QByteArray sizes;
    QByteArray rateLists;
    m_sizes.clear();
    for (quint16 index = 0; index < count; ++index) {
        const QByteArray size = Wire::slice(bytes, 32 + (qsizetype(index) * 8), 8);
        const quint16 rateCount = m_wire.word(bytes, rates);
        const QByteArray rateList = Wire::slice(bytes, rates, 2 + (rateCount * 2));
        rates += rateList.size();
        const QSize dimensions(m_wire.word(size, 0), m_wire.word(size, 2));
        if (dimensions == m_size) {
            if (dimensions == m_size) {
                current = kept;
                found = true;
            }
            m_sizes.insert(kept++, index);
            sizes += size;
            rateLists += rateList;
        }
    }
    if (!found) {
        throw std::runtime_error("Requested legacy size absent from server");
    }
    QByteArray result = Wire::slice(bytes, 0, 32) + sizes + rateLists;
    m_wire.word(result, 20, kept);
    m_wire.word(result, 22, current);
    m_wire.word(result, 28, static_cast<quint16>(rateLists.size() / 2));
    return result;
}

QByteArray DisplayReplies::vidmode(quint32 operation, QByteArray bytes)
{
    if (operation == 1) {
        if (m_currentTiming.isEmpty()) {
            throw std::runtime_error("Display policy requires RandR before VidMode current timing");
        }
        m_wire.integer(bytes, 8, m_wire.integer(m_currentTiming, 8) / 1000);
        const std::pair<int, int> fields[] = {{12, 4}, {14, 12}, {16, 14}, {18, 16}, {20, 18}, {22, 6}, {24, 20}, {26, 22}, {28, 24}};
        for (const auto &[destination, source] : fields) {
            m_wire.word(bytes, destination, m_wire.word(m_currentTiming, source));
        }
        m_wire.integer(bytes, 32, m_wire.integer(m_currentTiming, 28));
    } else if (operation == 6) {
        const quint32 count = m_wire.integer(bytes, 8);
        Wire::require(bytes, 0, 32);
        if (count > static_cast<quint64>((bytes.size() - 32) / 48)) {
            throw std::runtime_error("Truncated VidMode list");
        }
        QByteArray result = Wire::slice(bytes, 0, 32);
        quint32 kept = 0;
        for (quint32 index = 0; index < count; ++index) {
            const QByteArray mode = Wire::slice(bytes, 32 + (static_cast<qsizetype>(index) * 48), 48);
            if (m_wire.integer(mode, 44)) {
                throw std::runtime_error("Private VidMode data unsupported");
            }
            if (m_wire.word(mode, 4) == m_size.width() && m_wire.word(mode, 16) == m_size.height()) {
                result += mode;
                ++kept;
            }
        }
        m_wire.integer(result, 8, kept);
        return result;
    }
    return bytes;
}

} // namespace UpscaleX11
