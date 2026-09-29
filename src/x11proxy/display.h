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
    /**
     * The events that tell a client its screen changed to this view, for a
     * connection that saw another one until now: RRScreenChangeNotify where
     * @p selected on the root has it, RRNotify's CrtcChange where it has
     * that. Each carries @p sequence, the last the client read, so that no
     * client takes it for a later one. Empty where nothing is selected.
     */
    QByteArray changed(quint16 sequence, quint16 selected) const;
    quint16 nativeSizeIndex(quint16 index) const;
    quint32 root() const
    {
        return m_root;
    }
    int randrEvent = -1;

private:
    void disable(const char *reason);
    void dimensions(QByteArray &bytes, qsizetype offset, bool wide = false) const;
    QByteArray randr(quint32 operation, QByteArray bytes);
    QByteArray resources(const QByteArray &bytes);
    QByteArray outputInfo(const QByteArray &bytes);
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
    bool m_enabled = true;
};

} // namespace UpscaleX11
