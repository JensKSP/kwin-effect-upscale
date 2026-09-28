/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineprocess.h"

std::optional<WineProcess> wineProcess(qint64 pid)
{
    // No way to read another process's environment here yet; the game stays
    // as it is.
    Q_UNUSED(pid)
    return std::nullopt;
}
