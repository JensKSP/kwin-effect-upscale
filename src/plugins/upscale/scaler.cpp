/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "scaler.h"

#include "effect/effecthandler.h"
#include "opengl/glframebuffer.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "scene/surfaceitem.h"
#include "scene/workspacescene.h"

#include <QFile>
#include <QScopeGuard>

#include <array>

static void ensureResources()
{
    Q_INIT_RESOURCE(upscale);
}

namespace KWin
{

UpscaleScaler::UpscaleScaler(ItemRenderer *renderer)
    : m_renderer(renderer)
{
}
UpscaleScaler::~UpscaleScaler() = default;
UpscaleScaler::Buffer::Buffer() = default;
UpscaleScaler::Buffer::~Buffer() = default;

void UpscaleScaler::Buffer::release()
{
    framebuffer.reset();
    texture.reset();
}

bool UpscaleScaler::Buffer::resize(const QSize &size, GLenum internalFormat)
{
    GLint maximumSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    if (size.isEmpty() || size.width() > maximumSize || size.height() > maximumSize) {
        return false;
    }
    if (texture && texture->size() == size && format == internalFormat && framebuffer && framebuffer->valid()) {
        return true;
    }
    // A texture whose framebuffer could not be completed is of no use, and
    // keeping it would make every later attempt at this size fail as well.
    release();
    texture = allocateTexture(size, internalFormat);
    if (!texture) {
        return false;
    }
    format = internalFormat;
    texture->setFilter(GL_NEAREST);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);
    framebuffer = std::make_unique<GLFramebuffer>(texture.get());
    return framebuffer->valid();
}

// KWin hands a shader file to the compiler unchanged, so the preamble has to
// come from here. OpenGL ES 3.0 needs "#version 300 es", which desktop OpenGL
// rejects, and its fragment language defines no default precision for floats
// or samplers and only a medium one for integers. Medium precision would
// discard HDR detail while sampling and cannot address a 4K pixel grid.
// KWin versions that supply their own preamble ignore this one's directive.
static std::unique_ptr<GLShader> loadShader(const QString &path, bool direct = false)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const QByteArray preamble = usingOpenGLES()
        ? QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nprecision highp int;\n")
        : QByteArrayLiteral("#version 140\n");
    const QByteArray space = direct ? QByteArrayLiteral("#define UPSCALE_DIRECT 1\n") : QByteArray();
    return ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(),
                                                           preamble + space + file.readAll());
}

bool UpscaleScaler::initialize()
{
    ensureResources();
    m_easu = loadShader(QStringLiteral(":/effects/upscale/shaders/upscale.frag"));
    m_rcas = loadShader(QStringLiteral(":/effects/upscale/shaders/sharpen.frag"));
    m_easuDirect = loadShader(QStringLiteral(":/effects/upscale/shaders/upscale.frag"), true);
    m_rcasDirect = loadShader(QStringLiteral(":/effects/upscale/shaders/sharpen.frag"), true);
    return validShader(m_easu.get()) && validShader(m_rcas.get())
        && validShader(m_easuDirect.get()) && validShader(m_rcasDirect.get());
}

void UpscaleScaler::setColorUniforms(GLShader *shader, const RenderTarget &target)
{
    const ColorDescription &colors = targetColors(target);
    const TransferFunction transfer = colors.transferFunction();
    shader->setUniform("destinationTransferFunction", int(transfer.type));
    shader->setUniform("destinationLuminance", QVector2D(float(transfer.minLuminance), float(transfer.maxLuminance - transfer.minLuminance)));
    shader->setUniform("referenceLuminance", colors.referenceLuminance());
}

void UpscaleScaler::draw(GLShader *shader, GLTexture *texture, const RenderViewport &viewport,
                         const UpscaleRectF &destination, const UpscaleRegion &region)
{
    QMatrix4x4 matrix = viewport.projectionMatrix();
    matrix.translate(float(std::round(destination.x() * viewport.scale())), float(std::round(destination.y() * viewport.scale())));
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
    shader->setUniform(GLShader::IntUniform::Sampler, 0);
    const bool clipping = region != unlimitedRegion();
    if (clipping) {
        glEnable(GL_SCISSOR_TEST);
    }
    texture->bind();
    texture->render(region, destination.size() * viewport.scale(), clipping);
    texture->unbind();
    if (clipping) {
        glDisable(GL_SCISSOR_TEST);
    }
}

bool UpscaleScaler::render(const RenderTarget &target, const RenderViewport &viewport, SurfaceItem *surface,
                           const UpscaleRectF &destination, const UpscaleRegion &region, double strength)
{
    const QSize inputSize = surface->bufferSize();
    // The capture holds the destination encoding as the client committed it.
    // Only a linear destination needs more than ten bits a channel to do that
    // without loss, and only that one can carry values outside zero to one.
    if (!m_input.resize(inputSize, upscaleFilterFormat(targetColors(target)))) {
        return false;
    }

    // Render the original surface at one texel per source pixel. Going through
    // KWin's renderer retains buffer import, acquire synchronisation and release
    // fences. In particular, never sample a client texture behind its back.
    // Use exactly the original target description. Substituting a linear
    // transfer function here could turn an identity scRGB conversion into
    // KWin's colour shader path, whose tone mapper can clip negative values.
    // Transfer decoding for the filter domain happens in the EASU shader;
    // gamut conversion and display tone mapping still happen exactly once.
    const RenderTarget captureTarget(m_input.framebuffer.get(), target.colorDescription());
    const double scale = double(inputSize.width()) / surface->destinationSize().width();
    const RenderViewport inputViewport = captureViewport(UpscaleRectF(surface->position(), surface->destinationSize()), scale, captureTarget);
    const bool scissoring = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    const auto restoreScissor = qScopeGuard([scissoring]() {
        if (scissoring) {
            glEnable(GL_SCISSOR_TEST);
        }
    });
    GLFramebuffer::pushFramebuffer(m_input.framebuffer.get());
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    const bool captured = captureSurface(m_renderer, captureTarget, inputViewport, surface);
    GLFramebuffer::popFramebuffer();
    if (!captured) {
        return false;
    }
    return renderTexture(target, viewport, m_input.texture.get(), destination, region, strength);
}

bool UpscaleScaler::renderTexture(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
                                  const UpscaleRectF &destination, const UpscaleRegion &region, double strength)
{
    if (!supportsUpscaleColors(targetColors(target))) {
        return false;
    }
    const bool scissoring = glIsEnabled(GL_SCISSOR_TEST);
    std::array<GLint, 4> scissorBox;
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox.data());
    glDisable(GL_SCISSOR_TEST);
    const auto restoreScissor = qScopeGuard([scissoring, scissorBox]() {
        glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        if (scissoring) {
            glEnable(GL_SCISSOR_TEST);
        }
    });
    const QSize inputSize = input->size();
    const QSize outputSize = (destination.size() * viewport.scale()).toSize();
    if (strength > 0) {
        if (!m_scaled.resize(outputSize, upscaleFilterFormat(targetColors(target)))) {
            return false;
        }
    } else {
        // A destination-sized floating point image is the largest allocation
        // this effect makes. Switching sharpening off has to return it.
        m_scaled.release();
    }

    // The input is opaque. Preserve GL state also when KWin's item renderer
    // enabled blending for a buffer with an unused alpha channel.
    const bool blending = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);
    const bool direct = upscaleFiltersDirectly(targetColors(target));
    GLShader *easu = direct ? m_easuDirect.get() : m_easu.get();
    GLShader *rcas = direct ? m_rcasDirect.get() : m_rcas.get();
    {
        const ShaderBinder binder(easu);
        easu->setUniform("inputSize", QVector2D(float(inputSize.width()), float(inputSize.height())));
        easu->setUniform("outputSize", QVector2D(float(outputSize.width()), float(outputSize.height())));
        easu->setUniform("intermediate", int(strength > 0));
        // The direct shaders carry no transfer-function arithmetic, so they
        // declare none of these and asking for them would only log misses.
        if (!direct) {
            setColorUniforms(easu, target);
        }
        if (strength > 0) {
            const RenderTarget scaledTarget(m_scaled.framebuffer.get());
            const UpscaleRectF rectangle(QPointF(), outputSize);
            const RenderViewport scaledViewport = captureViewport(rectangle, 1, scaledTarget);
            GLFramebuffer::pushFramebuffer(m_scaled.framebuffer.get());
            draw(easu, input, scaledViewport, rectangle, unlimitedRegion());
            GLFramebuffer::popFramebuffer();
        } else {
            draw(easu, input, viewport, destination, region);
        }
    }
    if (strength > 0) {
        const ShaderBinder binder(rcas);
        rcas->setUniform("outputSize", QVector2D(float(outputSize.width()), float(outputSize.height())));
        rcas->setUniform("strength", strength);
        if (!direct) {
            setColorUniforms(rcas, target);
        }
        draw(rcas, m_scaled.texture.get(), viewport, destination, region);
    }
    if (blending) {
        glEnable(GL_BLEND);
    }
    return true;
}

} // namespace KWin
