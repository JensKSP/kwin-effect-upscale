// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "registry.h"

namespace UpscaleX11
{

quint64 Registry::insert(quint32 base, quint32 mask, quint32 pid)
{
    const quint64 generation = ++m_generation;
    m_clients.insert(base, {mask, pid, generation});
    return generation;
}

void Registry::remove(quint32 base, quint64 generation)
{
    // The server can reuse a resource range before an old relay drains.
    // That relay's destruction must not erase the replacement connection.
    const auto found = m_clients.constFind(base);
    if (found != m_clients.cend() && found->generation == generation) {
        m_clients.remove(base);
    }
}

std::optional<quint32> Registry::processFor(quint32 resource) const
{
    for (auto client = m_clients.cbegin(); client != m_clients.cend(); ++client) {
        if ((resource & ~client->mask) == client.key()) {
            return client->pid;
        }
    }
    return {};
}

} // namespace UpscaleX11
