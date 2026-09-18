/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effect.h"
#include "opengl/glshader.h"
#include "opengl/gltexture.h"
#include "scene/itemrenderer.h"

#include <QByteArrayView>

#include <cmath>

// KWin's development API replaced Qt regions and changed paint callbacks.
// Keep these differences here rather than in the scaling and colour logic.
#if __has_include("core/region.h")
#define UPSCALE_NEW_API 1
#include "opengl/eglcontext.h"
#else
#define UPSCALE_NEW_API 0
#include "opengl/openglcontext.h"
#endif

namespace KWin
{

#if UPSCALE_NEW_API
using UpscaleRegion = Region;
using UpscaleRect = Rect;
using UpscaleRectF = RectF;
using UpscalePaintResult = bool;
using UpscaleOutput = LogicalOutput;
#else
using UpscaleRegion = QRegion;
using UpscaleRect = QRect;
using UpscaleRectF = QRectF;
using UpscalePaintResult = void;
using UpscaleOutput = Output;
#endif

inline UpscaleRegion unlimitedRegion()
{
#if UPSCALE_NEW_API
    return Region::infinite();
#else
    return infiniteRegion();
#endif
}

inline bool validShader(GLShader *shader)
{
#if UPSCALE_NEW_API
    return shader != nullptr;
#else
    return shader && shader->isValid();
#endif
}

inline std::unique_ptr<GLTexture> allocateFloatTexture(const QSize &size)
{
    std::unique_ptr<GLTexture> texture = GLTexture::allocate(GL_RGBA32F, size);
#if UPSCALE_NEW_API
    if (texture) {
#else
    if (texture && OpenGlContext::currentContext()->isOpenGLES()) {
#endif
        // KWin's GLES allocator (also used exclusively by current master)
        // creates 8-bit storage even when internalFormat() reports RGBA32F.
        // Replace that mutable storage with the requested format; framebuffer
        // completeness checks whether the implementation can render to it.
        texture->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size.width(), size.height(), 0, GL_RGBA, GL_FLOAT, nullptr);
        texture->unbind();
        // A failed replacement can leave the original 8-bit image intact and
        // framebuffer-complete. Never silently filter HDR through that image.
        if (glGetError() != GL_NO_ERROR) {
            return nullptr;
        }
    }
    return texture;
}

// GL_VERSION always starts with "OpenGL ES" on an OpenGL ES implementation.
// Asking the context itself would need a different KWin class per version.
inline bool usingOpenGLES()
{
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    return version && QByteArrayView(version).startsWith("OpenGL ES");
}

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
    return renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, unlimitedRegion(), data, {}, {});
#else
    renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, unlimitedRegion(), data);
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

// The shaders select their transfer function by number. KWin has kept these
// values stable and only appended to the enumeration; anything it adds later
// is rejected below rather than silently decoded with the wrong curve.
static_assert(int(TransferFunction::sRGB) == 0);
static_assert(int(TransferFunction::linear) == 1);
static_assert(int(TransferFunction::PerceptualQuantizer) == 2);
static_assert(int(TransferFunction::gamma22) == 3);

inline bool supportsUpscaleColors(const ColorDescription &colors)
{
    const TransferFunction transfer = colors.transferFunction();
    if (!std::isfinite(colors.referenceLuminance()) || colors.referenceLuminance() <= 0
        || !std::isfinite(transfer.minLuminance) || !std::isfinite(transfer.maxLuminance)
        || transfer.maxLuminance <= transfer.minLuminance) {
        return false;
    }
    switch (transfer.type) {
    case TransferFunction::sRGB:
    case TransferFunction::linear:
    case TransferFunction::PerceptualQuantizer:
    case TransferFunction::gamma22:
        return true;
    default:
        return false;
    }
}

} // namespace KWin
