// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
namespace UpscaleX11
{
/*
 * Where a process's command line and environment cannot be read, nothing is
 * claimed about what it runs. Every connection then keeps the display the
 * server itself reports, which is the behaviour without this proxy at all.
 */
ProgramIdentity upscaleProgramIdentity(quint32)
{
    return {};
}

QString upscalePrefixProgram(const QString &)
{
    return {};
}
}
