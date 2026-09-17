/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(UpscaleEffect,
                              "metadata.json.stripped",
                              return UpscaleEffect::supported();)

} // namespace KWin

#include "main.moc"
