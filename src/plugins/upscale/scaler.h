/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"
#include "picture.h"

#include <memory>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class SurfaceItem;

/**
 * One window's picture as the scaler draws it: into @c destination, filtered
 * as @c filter says and sharpened by @c strength, with what of @c frame it
 * leaves painted black, the bars an aspect ratio or a whole factor leaves. An
 * empty frame draws the picture alone.
 */
struct UpscaleDrawing
{
    UpscaleRectF destination;
    double strength = 0;
    UpscaleFilter filter = UpscaleFilter::Fsr;
    UpscaleRectF frame;
};

class UpscaleScaler
{
public:
    explicit UpscaleScaler(ItemRenderer *renderer);
    ~UpscaleScaler();

    bool initialize();
    bool render(const RenderTarget &target, const RenderViewport &viewport, SurfaceItem *surface,
                const UpscaleDrawing &drawing, const UpscaleRegion &region);
    // Input uses the destination colour description. Kept separate from
    // capture so colour and sampling can be tested
    // with floating-point fixtures without a window-system buffer import.
    bool renderTexture(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
                       const UpscaleDrawing &drawing, const UpscaleRegion &region);

private:
    struct Buffer
    {
        Buffer();
        ~Buffer();
        bool resize(const QSize &size, GLenum internalFormat);
        void release();
        GLenum format = 0;
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };

    void scale(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
               const UpscaleRectF &destination, const UpscaleRegion &region, double strength);
    void sharpen(const RenderTarget &target, const RenderViewport &viewport,
                 const UpscaleRectF &destination, const UpscaleRegion &region, double strength);
    void bars(const RenderViewport &viewport, const UpscaleRectF &frame, const UpscaleRectF &destination,
              const UpscaleRegion &region);
    static void setColorUniforms(GLShader *shader, const RenderTarget &target);
    static void draw(GLShader *shader, GLTexture *texture, const RenderViewport &viewport,
                     const UpscaleRectF &destination, const UpscaleRegion &region);

    Buffer m_input;
    Buffer m_scaled;
    // One pair per filter space. Selecting with a uniform instead would keep
    // the transfer-function arithmetic in the shader that does not need it.
    std::unique_ptr<GLShader> m_easu;
    std::unique_ptr<GLShader> m_rcas;
    std::unique_ptr<GLShader> m_easuDirect;
    std::unique_ptr<GLShader> m_rcasDirect;
    // One black pixel, stretched over each bar.
    std::unique_ptr<GLTexture> m_black;
    ItemRenderer *m_renderer;
};

} // namespace KWin
