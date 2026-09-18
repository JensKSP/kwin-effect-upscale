/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <memory>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class SurfaceItem;

class UpscaleScaler
{
public:
    explicit UpscaleScaler(ItemRenderer *renderer);
    ~UpscaleScaler();

    bool initialize();
    bool render(const RenderTarget &target, const RenderViewport &viewport, SurfaceItem *surface,
                const UpscaleRectF &destination, const UpscaleRegion &region, double strength);
    // Input uses the destination colour description. Kept separate from
    // capture so colour and sampling can be tested
    // with floating-point fixtures without a window-system buffer import.
    bool renderTexture(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
                       const UpscaleRectF &destination, const UpscaleRegion &region, double strength);

private:
    struct Buffer
    {
        Buffer();
        ~Buffer();
        bool resize(const QSize &size);
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };

    static void setColorUniforms(GLShader *shader, const RenderTarget &target);
    static void draw(GLShader *shader, GLTexture *texture, const RenderViewport &viewport,
                     const UpscaleRectF &destination, const UpscaleRegion &region);

    Buffer m_input;
    Buffer m_scaled;
    std::unique_ptr<GLShader> m_easu;
    std::unique_ptr<GLShader> m_rcas;
    ItemRenderer *m_renderer;
};

} // namespace KWin
