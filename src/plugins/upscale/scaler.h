/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"
#include "picture.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <memory>
#include <utility>

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

/** Whether RCAS sharpens what @p drawing's filter draws. */
inline bool upscaleRcasFollows(const UpscaleDrawing &drawing)
{
    return drawing.strength > 0 && upscaleFilterSharpensWithRcas(drawing.filter);
}

/**
 * Whether @p filter's program, compiled for a destination it can filter
 * directly or not, decodes the destination's encoding. xBRZ always does: it
 * measures how far apart two colours are in linear light, whatever the
 * encoding they arrived in.
 */
inline bool upscaleProgramDecodes(UpscaleFilter filter, bool direct)
{
    return !direct || filter == UpscaleFilter::Xbrz;
}

/** The file and the defines each filter's program is made of. */
std::pair<QString, QByteArray> upscaleFilterSource(UpscaleFilter filter);

/**
 * What @p shader, @p drawing's filter, is told before it draws an @p input
 * of that size at @p output into a destination of @p colors, other than the
 * colour description itself, which the scaler sets where the program decodes;
 * @p intermediate where it writes the image RCAS sharpens afterwards.
 */
void upscaleSetFilterUniforms(GLShader *shader, const UpscaleDrawing &drawing, const QSize &input, const QSize &output,
                              const ColorDescription &colors, bool intermediate);

class UpscaleScaler
{
public:
    explicit UpscaleScaler(ItemRenderer *renderer);
    ~UpscaleScaler();

    bool initialize();
    // The picture at @p picture's own size, captured from @p surface, the
    // window's own, with what lies on it: the two are one unless the picture
    // is a subsurface covering the window, which is then captured over the
    // window's buffer as KWin composites them.
    bool render(const RenderTarget &target, const RenderViewport &viewport, SurfaceItem *picture, SurfaceItem *surface,
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

    bool scale(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
               const UpscaleDrawing &drawing, const UpscaleRegion &region);
    void sharpen(const RenderTarget &target, const RenderViewport &viewport,
                 const UpscaleRectF &destination, const UpscaleRegion &region, double strength);
    void bars(const RenderViewport &viewport, const UpscaleRectF &frame, const UpscaleRectF &destination,
              const UpscaleRegion &region);
    static void setColorUniforms(GLShader *shader, const RenderTarget &target);
    static void draw(GLShader *shader, GLTexture *texture, const RenderViewport &viewport,
                     const UpscaleRectF &destination, const UpscaleRegion &region);

    // One program per filter space. Selecting with a uniform instead would
    // keep the transfer-function arithmetic in the shader that does not need it.
    struct Programs
    {
        std::unique_ptr<GLShader> working;
        std::unique_ptr<GLShader> direct;
        bool loaded = false;
    };
    // A filter's program for the space it filters in, compiled when the
    // filter is first used, so that a session pays only for what it chose.
    GLShader *program(UpscaleFilter filter, bool direct);

    Buffer m_input;
    Buffer m_scaled;
    // Nearest neighbour needs none, and draws with KWin's own.
    std::array<Programs, std::size_t(upscaleLastFilter) + 1> m_filters;
    std::unique_ptr<GLShader> m_rcas;
    std::unique_ptr<GLShader> m_rcasDirect;
    // One black pixel, stretched over each bar.
    std::unique_ptr<GLTexture> m_black;
    ItemRenderer *m_renderer;
};

} // namespace KWin
