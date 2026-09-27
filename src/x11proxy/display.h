// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "wire.h"
#include <QHash>
#include <QSet>
#include <QSize>

namespace UpscaleX11
{

// Static display view for a selected connection on a single output. No compositor input transformation.
class DisplayReplies
{
public:
    DisplayReplies(Wire &wire, QSize size, QByteArray timing = {});
    void setup(QByteArray &bytes);
    void request(quint8 operation, QByteArray &bytes, qsizetype shift);
    QByteArray reply(const QByteArray &kind, quint32 operation, QByteArray bytes);
    void event(QByteArray &bytes);
    quint16 nativeSizeIndex(quint16 index) const;
    int randrEvent = -1;

private:
    void dimensions(QByteArray &bytes, qsizetype offset, bool wide = false) const;
    QByteArray randr(quint32 operation, QByteArray bytes);
    QByteArray resources(const QByteArray &bytes);
    QByteArray screenInfo(const QByteArray &bytes);
    QByteArray vidmode(quint32 operation, QByteArray bytes);
    Wire &m_wire;
    QSize m_size;
    quint32 m_root = 0;
    QSet<quint32> m_modes;
    quint32 m_currentMode = 0;
    QByteArray m_currentTiming;
    QByteArray m_initialTiming;
    QHash<quint16, quint16> m_sizes;
    QSet<QByteArray> m_reported;
};

} // namespace UpscaleX11
