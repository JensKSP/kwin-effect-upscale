/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "egl_factory.h"
#include "scaler.h"

#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"
#include "opengl/glframebuffer.h"
#include "opengl/gltexture.h"
#include "opengl/glvertexbuffer.h"

#include <QDebug>
#include <QSize>

#include <memory>
#include <vector>

namespace KWin
{

/**
 * The headless GPU the render tests draw on, and the one call that drives the
 * scaler over it.
 *
 * Shared by more than one test program, so that adding a test does not mean
 * another copy of the EGL setup, and so that every test sees the same scaler
 * the effect itself builds.
 */
struct UpscaleRenderFixture
{
    /** False when this machine has no headless GPU or no working shaders. */
    bool initialize();
    void release();

    /**
     * Scale @p pixels and read the result back in storage order.
     *
     * @p targetTransform is the orientation KWin would compose into; its DRM
     * backend flips every frame, so a test can ask for that here. A @p picture
     * is laid inside the output with bars around it, sampled with @p filter.
     */
    std::vector<float> render(const std::vector<float> &pixels, const QSize &inputSize, const QSize &outputSize,
                              TransferFunction transfer, double strength,
                              const UpscaleRegion &region = unlimitedRegion(),
                              OutputTransform targetTransform = OutputTransform::Normal, const QRect &picture = {},
                              UpscaleFilter filter = UpscaleFilter::Fsr);

    std::unique_ptr<EglDisplay> m_display;
    std::shared_ptr<EglContext> m_context;
    std::unique_ptr<UpscaleScaler> m_scaler;
};

inline bool UpscaleRenderFixture::initialize()
{
    // A headless EGL display exercises KWin's real shader manager and textures
    // under Mesa. It does not establish compositor lifecycle or hardware VRR.
    const EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    m_display = createEglDisplay<EglDisplay>(display);
    if (!m_display) {
        return false;
    }
    m_context = createEglContext<EglContext>(m_display.get());
    if (!m_context) {
        return false;
    }
    qInfo() << "OpenGL:" << reinterpret_cast<const char *>(glGetString(GL_VERSION));
    m_scaler = std::make_unique<UpscaleScaler>(nullptr);
    return m_scaler->initialize();
}

inline void UpscaleRenderFixture::release()
{
    m_scaler.reset();
    m_context.reset();
    m_display.reset();
}

inline std::vector<float> UpscaleRenderFixture::render(const std::vector<float> &pixels, const QSize &inputSize, const QSize &outputSize,
                                                       TransferFunction transfer, double strength, const UpscaleRegion &region,
                                                       OutputTransform targetTransform, const QRect &picture, UpscaleFilter filter)
{
    std::unique_ptr<GLTexture> input = allocateFloatTexture(inputSize);
    std::unique_ptr<GLTexture> output = allocateFloatTexture(outputSize);
    if (!input || !output) {
        return {};
    }
    input->bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inputSize.width(), inputSize.height(), GL_RGBA, GL_FLOAT, pixels.data());
    input->unbind();
    // KWin's DRM backend gives the screen pass a flipped target, so the
    // fixture has to be able to produce one: RenderTarget takes the transform
    // from the colour attachment, which must therefore be set before it.
    output->setContentTransform(targetTransform);
    GLFramebuffer framebuffer(output.get());
    if (!framebuffer.valid()) {
        return {};
    }
#if UPSCALE_REGION_API
    const auto colors = ColorDescription::sRGB->withTransferFunction(transfer);
#else
    const auto colors = ColorDescription::sRGB.withTransferFunction(transfer);
#endif
    const RenderTarget target(&framebuffer, colors);
    const UpscaleRectF rectangle(QPointF(), outputSize);
    const RenderViewport viewport = captureViewport(rectangle, 1, target);
    GLFramebuffer::pushFramebuffer(&framebuffer);
    GLVertexBuffer::streamingBuffer()->beginFrame();
    glClearColor(-7, -7, -7, -7);
    glClear(GL_COLOR_BUFFER_BIT);
    const UpscaleDrawing drawing{
        .destination = picture.isEmpty() ? rectangle : UpscaleRectF(picture),
        .strength = strength,
        .filter = filter,
        .frame = picture.isEmpty() ? UpscaleRectF() : rectangle,
    };
    const bool success = m_scaler->renderTexture(target, viewport, input.get(), drawing, region);
    std::vector<float> result(size_t(outputSize.width()) * size_t(outputSize.height()) * 4);
    glReadPixels(0, 0, outputSize.width(), outputSize.height(), GL_RGBA, GL_FLOAT, result.data());
    GLVertexBuffer::streamingBuffer()->endOfFrame();
    GLFramebuffer::popFramebuffer();
    if (!success || glGetError() != GL_NO_ERROR) {
        return {};
    }
    return result;
}

} // namespace KWin
