// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <optional>

namespace UpscaleX11
{

// Xwayland sees the proxy's PID on forwarded sockets. Keep the authenticated
// client's resource range so XRes still identifies the original process.
class Registry
{
public:
    quint64 insert(quint32 base, quint32 mask, quint32 pid);
    void remove(quint32 base, quint64 generation);
    std::optional<quint32> processFor(quint32 resource) const;

private:
    struct Client
    {
        quint32 mask;
        quint32 pid;
        quint64 generation;
    };
    QHash<quint32, Client> m_clients;
    quint64 m_generation = 0;
};

} // namespace UpscaleX11
