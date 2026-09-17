/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "effect/effect.h"

namespace KWin
{

/**
 * Upscales fullscreen windows whose buffer is smaller than the output they
 * cover, so that the game picks the resolution and the compositor only decides
 * how the image is enlarged.
 *
 * At this point the effect is a skeleton: it loads, reports itself inactive and
 * changes nothing.
 */
class UpscaleEffect : public Effect
{
    Q_OBJECT

public:
    UpscaleEffect();
    ~UpscaleEffect() override;

    static bool supported();

    bool isActive() const override;
    int requestedEffectChainPosition() const override;
};

} // namespace KWin
