/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effect.h"
#include "scene/itemrenderer.h"

// KWin's development API replaced Qt regions and changed paint callbacks.
// Keep these differences here rather than in the scaling and colour logic.
#if __has_include("core/region.h")
#define UPSCALE_NEW_API 1
#else
#define UPSCALE_NEW_API 0
#endif

namespace KWin
{

#if UPSCALE_NEW_API
using UpscaleRegion = Region;
using UpscaleRect = Rect;
using UpscaleRectF = RectF;
using UpscalePaintResult = bool;
#else
using UpscaleRegion = QRegion;
using UpscaleRect = QRect;
using UpscaleRectF = QRectF;
using UpscalePaintResult = void;
#endif

inline RenderViewport captureViewport(const UpscaleRectF &geometry, double scale, const RenderTarget &target)
{
#if UPSCALE_NEW_API
    return RenderViewport(geometry, scale, target, QPoint());
#else
    return RenderViewport(geometry, scale, target);
#endif
}

inline bool captureSurface(ItemRenderer *renderer, const RenderTarget &target, const RenderViewport &viewport, Item *surface)
{
    const WindowPaintData data;
#if UPSCALE_NEW_API
    return renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, infiniteRegion(), data, {}, {});
#else
    renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, infiniteRegion(), data);
    return true;
#endif
}

inline const ColorDescription &targetColors(const RenderTarget &target)
{
#if UPSCALE_NEW_API
    return *target.colorDescription();
#else
    return target.colorDescription();
#endif
}

} // namespace KWin
