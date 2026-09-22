/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/renderloop.h"
#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effect.h"
#include "opengl/glshader.h"
#include "opengl/gltexture.h"
#include "scene/itemrenderer.h"

#include <QByteArrayView>

#include <cmath>

// KWin 6.6 replaced Qt regions and shared its colour descriptions. The later
// render-device API changed paint callbacks and removed desktop OpenGL.
// Detect these boundaries separately: a region header alone does not imply
// render devices. Keep the differences out of scaling and colour logic.
#if __has_include("core/region.h")
#define UPSCALE_REGION_API 1
#include "opengl/eglcontext.h"
#else
#define UPSCALE_REGION_API 0
#include "opengl/openglcontext.h"
#endif

// Presentation timing lives on the output the compositor drives, which later
// KWin declares in a header of its own. That header uses std::expected, which
// not every compiler this has to build with offers yet, and a toolchain that
// cannot compile KWin's header cannot be made to by anything here. Where it is
// unavailable the frames are simply not measured and the reports say so,
// rather than the build failing over a measurement.
#include <version>
#if !UPSCALE_REGION_API
#define UPSCALE_PRESENTATION_API 1
#elif defined(__cpp_lib_expected)
#define UPSCALE_PRESENTATION_API 1
#include "core/backendoutput.h"
#else
#define UPSCALE_PRESENTATION_API 0
#endif

// Whether the effect callbacks return bool, RenderView offers renderDevice()
// and renderItem() reports failure. These arrived together, so one switch
// covers them, but which one it is cannot be told from a file name:
// core/renderdevice.h is present in KWin 6.7, whose callbacks still return
// void. The build compiles KWin's own declaration to find out and defines this
// from the answer. Left undefined - building inside KWin's tree, where the
// headers beside this file are the current ones - the newest API is correct.
#ifndef UPSCALE_RENDER_DEVICE_API
#define UPSCALE_RENDER_DEVICE_API 1
#endif
// Whether an effect that intercepts the pointer receives KWin's pointer events
// (pointerMotion, pointerButton) rather than Qt mouse events through
// windowInputMouseEvent. Decided by the build the same way, and the newest
// API inside KWin's tree.
#ifndef UPSCALE_POINTER_EVENT_API
#define UPSCALE_POINTER_EVENT_API 1
#endif

namespace KWin
{

#if UPSCALE_REGION_API
using UpscaleRegion = Region;
using UpscaleRect = Rect;
using UpscaleRectF = RectF;
using UpscaleOutput = LogicalOutput;
#else
using UpscaleRegion = QRegion;
using UpscaleRect = QRect;
using UpscaleRectF = QRectF;
using UpscaleOutput = Output;
#endif

#if UPSCALE_RENDER_DEVICE_API
using UpscalePaintResult = bool;
#else
using UpscalePaintResult = void;
#endif

inline UpscaleRegion unlimitedRegion()
{
#if UPSCALE_REGION_API
    return Region::infinite();
#else
    return infiniteRegion();
#endif
}

// KWin dropped GLShader::isValid() when a failed compile stopped producing a
// shader at all. That happened on its own schedule, earlier than the effect
// API change above, so it gets its own question rather than sharing that one's
// answer: 6.7 has no isValid() and still has the old paint callbacks.
//
// A template because if constexpr only discards the branch it is not taking
// inside one; in a plain function the call would still have to compile.
template<typename Shader>
inline bool validShader(Shader *shader)
{
    if constexpr (requires { shader->isValid(); }) {
        return shader && shader->isValid();
    } else {
        return shader != nullptr;
    }
}

// GL_VERSION always starts with "OpenGL ES" on an OpenGL ES implementation.
// Asking the context itself would need a different KWin class per version.
inline bool usingOpenGLES()
{
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    return version && QByteArrayView(version).startsWith("OpenGL ES");
}

// AMD asks for 32 bits per pixel for the images FSR 1 reads and writes, "for
// performance purposes". The working encoding this effect filters in is
// bounded to zero..one by construction, so an unsigned format can hold it and
// the only question is how many bits it needs. The capture is different: it
// holds whatever the destination encodes, which for a linear target includes
// values outside zero..one, so it stays floating point.
inline std::unique_ptr<GLTexture> allocateTexture(const QSize &size, GLenum internalFormat)
{
    std::unique_ptr<GLTexture> texture = GLTexture::allocate(internalFormat, size);
    if (texture && usingOpenGLES()) {
        // KWin's GLES allocator (also used exclusively by current master)
        // creates 8-bit storage even when internalFormat() reports otherwise.
        // Replace that mutable storage with the requested format; framebuffer
        // completeness checks whether the implementation can render to it.
        GLenum type = GL_FLOAT;
        if (internalFormat == GL_RGB10_A2) {
            type = GL_UNSIGNED_INT_2_10_10_10_REV;
        } else if (internalFormat == GL_RGBA16F) {
            type = GL_HALF_FLOAT;
        }
        // GL errors belong to the shared context. Discard earlier errors so
        // only this storage replacement can make the allocation fail.
        while (glGetError() != GL_NO_ERROR) {
        }
        texture->bind();
        glTexImage2D(GL_TEXTURE_2D, 0, GLint(internalFormat), size.width(), size.height(), 0, GL_RGBA, type, nullptr);
        texture->unbind();
        // A failed replacement can leave the original 8-bit image intact and
        // framebuffer-complete. Never silently filter HDR through that image.
        if (glGetError() != GL_NO_ERROR) {
            return nullptr;
        }
    }
    return texture;
}

inline std::unique_ptr<GLTexture> allocateFloatTexture(const QSize &size)
{
    return allocateTexture(size, GL_RGBA32F);
}

/**
 * Whether the filter can work in the destination's own encoding.
 *
 * EASU asks for an image that is already perceptually encoded and inside zero
 * to one. Every transfer function here except the linear one produces exactly
 * that, so for those the captured image already satisfies the requirement and
 * needs no conversion at all: the filter reads what the client committed.
 *
 * A linear destination is the exception. Its values run below zero, for
 * colours outside the gamut, and above one, for highlights, so they have to be
 * folded into the bounded working encoding before EASU will accept them.
 */
// The frames a screen really put in front of the user, and how it presented
// them. KWin renamed the output that owns this between the supported versions;
// the timing itself is the same signal on both.
inline RenderLoop *upscaleRenderLoop(UpscaleOutput *output)
{
#if !UPSCALE_PRESENTATION_API
    Q_UNUSED(output)
    return nullptr;
#elif UPSCALE_REGION_API
    return output && output->backendOutput() ? output->backendOutput()->renderLoop() : nullptr;
#else
    return output ? output->renderLoop() : nullptr;
#endif
}

inline bool upscaleFiltersDirectly(const ColorDescription &colors)
{
    return colors.transferFunction().type != TransferFunction::linear;
}

/**
 * The format for the images the filter reads and writes.
 *
 * AMD asks for 32 bits per pixel "for performance purposes", and in the
 * destination's own encoding that is what this uses: ten bits per channel over
 * a range the encoding actually fills, which is at least the depth the client
 * committed and the depth the screen will show.
 *
 * The working encoding cannot go there, and the reason is the encoding rather
 * than the format. It folds an unbounded signed range into zero to one, so
 * ordinary content occupies about a third of the code values and half of them
 * are reserved for negative linear light. Measured on 2026-09-18: at half
 * precision the colour tests fail by about 0.001 in the encoded output,
 * including at an sRGB mid grey, and an HDR value of 40 returns as 38.7. Ten
 * bit would be twice as coarse again.
 */
inline GLenum upscaleFilterFormat(const ColorDescription &colors)
{
    return upscaleFiltersDirectly(colors) ? GL_RGB10_A2 : GL_RGBA32F;
}

inline RenderViewport captureViewport(const UpscaleRectF &geometry, double scale, const RenderTarget &target)
{
#if UPSCALE_REGION_API
    return RenderViewport(geometry, scale, target, QPoint());
#else
    return RenderViewport(geometry, scale, target);
#endif
}

inline bool captureSurface(ItemRenderer *renderer, const RenderTarget &target, const RenderViewport &viewport, Item *surface)
{
    const WindowPaintData data;
#if UPSCALE_RENDER_DEVICE_API
    return renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, unlimitedRegion(), data, {}, {});
#elif UPSCALE_REGION_API
    renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, unlimitedRegion(), data, {}, {});
    return true;
#else
    renderer->renderItem(target, viewport, surface, Effect::PAINT_WINDOW_TRANSFORMED, unlimitedRegion(), data);
    return true;
#endif
}

inline const ColorDescription &targetColors(const RenderTarget &target)
{
#if UPSCALE_REGION_API
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
