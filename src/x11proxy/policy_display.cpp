// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"

namespace UpscaleX11
{

QByteArray Policy::changeDisplay(QSize size, const QByteArray &timing)
{
    if (!changesDisplay(size)) {
        return {};
    }
    m_size = size;
    m_display = std::make_unique<DisplayReplies>(m_wire, size, timing);
    m_display->randrEvent = m_randrEvent;
    if (m_setupReply.isEmpty()) {
        // Still starting: the setup reply will show the new size itself.
        return {};
    }
    // Learnt from a copy, since the client read the setup reply long ago.
    QByteArray setup = m_setupReply;
    m_display->setup(setup);
    QByteArray events = m_display->changed(m_lastSequence, m_randrSelections.value(m_display->root()));
    if (m_framer.pending(1)) {
        m_deferred += std::exchange(events, {});
    }
    return events;
}

} // namespace UpscaleX11
