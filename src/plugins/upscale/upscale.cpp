/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale.h"

#include "effect/effecthandler.h"

namespace KWin
{

UpscaleEffect::UpscaleEffect() = default;

UpscaleEffect::~UpscaleEffect() = default;

bool UpscaleEffect::supported()
{
    return effects->isOpenGLCompositing();
}

bool UpscaleEffect::isActive() const
{
    return false;
}

int UpscaleEffect::requestedEffectChainPosition() const
{
    return 99;
}

} // namespace KWin
