// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QtEndian>
#include <stdexcept>

namespace UpscaleX11
{

class Wire
{
public:
    bool little = true;
    static void require(const QByteArray &bytes, qsizetype offset, qsizetype count)
    {
        if (offset < 0 || count < 0 || offset > bytes.size() || count > bytes.size() - offset) {
            throw std::runtime_error("Truncated protocol record");
        }
    }
    static quint8 byte(const QByteArray &bytes, qsizetype offset)
    {
        require(bytes, offset, 1);
        return static_cast<quint8>(bytes[offset]);
    }
    quint16 word(const QByteArray &bytes, qsizetype offset) const
    {
        require(bytes, offset, 2);
        return little ? qFromLittleEndian<quint16>(bytes.constData() + offset) : qFromBigEndian<quint16>(bytes.constData() + offset);
    }
    quint32 integer(const QByteArray &bytes, qsizetype offset) const
    {
        require(bytes, offset, 4);
        return little ? qFromLittleEndian<quint32>(bytes.constData() + offset) : qFromBigEndian<quint32>(bytes.constData() + offset);
    }
    void word(QByteArray &bytes, qsizetype offset, quint16 value) const
    {
        require(bytes, offset, 2);
        if (little) {
            qToLittleEndian(value, bytes.data() + offset);
        } else {
            qToBigEndian(value, bytes.data() + offset);
        }
    }
    void integer(QByteArray &bytes, qsizetype offset, quint32 value) const
    {
        require(bytes, offset, 4);
        if (little) {
            qToLittleEndian(value, bytes.data() + offset);
        } else {
            qToBigEndian(value, bytes.data() + offset);
        }
    }
    static QByteArray slice(const QByteArray &bytes, qsizetype offset, qsizetype count)
    {
        require(bytes, offset, count);
        return bytes.mid(offset, count);
    }
};

} // namespace UpscaleX11
