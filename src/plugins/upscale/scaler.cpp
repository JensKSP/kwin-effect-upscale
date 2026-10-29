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
#include <QImage>
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
static std::unique_ptr<GLShader> loadShader(const QString &path, bool direct = false, const QByteArray &defines = QByteArray())
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const QByteArray preamble = usingOpenGLES()
        ? QByteArrayLiteral("#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nprecision highp int;\n")
        : QByteArrayLiteral("#version 140\n");
    const QByteArray space = direct ? QByteArrayLiteral("#define UPSCALE_DIRECT 1\n") : QByteArray();
    std::unique_ptr<GLShader> shader = ShaderManager::instance()->generateCustomShader(ShaderTrait::MapTexture, QByteArray(),
                                                                                       preamble + space + defines + file.readAll());
    return validShader(shader.get()) ? std::move(shader) : nullptr;
}

GLShader *UpscaleScaler::program(UpscaleFilter filter, bool direct)
{
    Programs &programs = m_filters[std::size_t(filter)];
    if (!programs.loaded) {
        const auto [path, defines] = upscaleFilterSource(filter);
        programs.working = loadShader(path, false, defines);
        programs.direct = loadShader(path, true, defines);
        programs.loaded = true;
    }
    return direct ? programs.direct.get() : programs.working.get();
}

bool UpscaleScaler::initialize()
{
    ensureResources();
    m_rcas = loadShader(QStringLiteral(":/effects/upscale/shaders/sharpen.frag"));
    m_rcasDirect = loadShader(QStringLiteral(":/effects/upscale/shaders/sharpen.frag"), true);
    QImage black(1, 1, QImage::Format_RGB32);
    black.fill(Qt::black);
    m_black = GLTexture::upload(black);
    // FSR is the default, and its programs are compiled now so that a driver
    // that cannot compile them is found before the first frame needs them.
    return m_black && m_rcas && m_rcasDirect && program(UpscaleFilter::Fsr, false) && program(UpscaleFilter::Fsr, true);
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

bool UpscaleScaler::render(const RenderTarget &target, const RenderViewport &viewport, SurfaceItem *picture,
                           SurfaceItem *surface, const UpscaleDrawing &drawing, const UpscaleRegion &region)
{
    const QSize inputSize = upscaleSuppliedSize(picture);
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
    return renderTexture(target, viewport, m_input.texture.get(), drawing, region);
}

// The rest of the frame, in up to four bars around the picture. Black is zero
// in every encoding a target can have, so the pixel needs no conversion.
void UpscaleScaler::bars(const RenderViewport &viewport, const UpscaleRectF &frame, const UpscaleRectF &destination,
                         const UpscaleRegion &region)
{
    const std::array<UpscaleRectF, 4> parts{
        UpscaleRectF(frame.left(), frame.top(), frame.width(), destination.top() - frame.top()),
        UpscaleRectF(frame.left(), destination.bottom(), frame.width(), frame.bottom() - destination.bottom()),
        UpscaleRectF(frame.left(), destination.top(), destination.left() - frame.left(), destination.height()),
        UpscaleRectF(destination.right(), destination.top(), frame.right() - destination.right(), destination.height()),
    };
    ShaderBinder binder(ShaderTrait::MapTexture);
    for (const UpscaleRectF &part : parts) {
        if (part.width() > 0 && part.height() > 0) {
            draw(binder.shader(), m_black.get(), viewport, part, region);
        }
    }
}

// The enlargement. Where RCAS is to sharpen it, it writes an intermediate for
// RCAS to sharpen in place afterwards, which is the order AMD specifies and
// the only one RCAS supports; otherwise, and for a filter that sharpens as part
// of itself, it draws straight to the destination.
bool UpscaleScaler::scale(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
                          const UpscaleDrawing &drawing, const UpscaleRegion &region)
{
    const QSize outputSize = (drawing.destination.size() * viewport.scale()).toSize();
    const bool direct = upscaleFiltersDirectly(targetColors(target));
    GLShader *filter = program(drawing.filter, direct);
    if (!filter) {
        return false;
    }
    const bool intermediate = upscaleRcasFollows(drawing);
    const ShaderBinder binder(filter);
    upscaleSetFilterUniforms(filter, drawing, input->size(), outputSize, targetColors(target), intermediate);
    // The direct shaders carry no transfer-function arithmetic, so they
    // declare none of these and asking for them would only log misses.
    if (!direct) {
        setColorUniforms(filter, target);
    }
    if (!intermediate) {
        draw(filter, input, viewport, drawing.destination, region);
        return true;
    }
    const RenderTarget scaledTarget(m_scaled.framebuffer.get());
    const UpscaleRectF rectangle(QPointF(), outputSize);
    const RenderViewport scaledViewport = captureViewport(rectangle, 1, scaledTarget);
    GLFramebuffer::pushFramebuffer(m_scaled.framebuffer.get());
    draw(filter, input, scaledViewport, rectangle, unlimitedRegion());
    GLFramebuffer::popFramebuffer();
    return true;
}

// RCAS, which sharpens without scaling and only ever runs on what a filter
// produced. That order is the one AMD specifies and the only one it supports.
void UpscaleScaler::sharpen(const RenderTarget &target, const RenderViewport &viewport,
                            const UpscaleRectF &destination, const UpscaleRegion &region, double strength)
{
    const QSize outputSize = (destination.size() * viewport.scale()).toSize();
    const bool direct = upscaleFiltersDirectly(targetColors(target));
    GLShader *rcas = direct ? m_rcasDirect.get() : m_rcas.get();
    const ShaderBinder binder(rcas);
    rcas->setUniform("outputSize", QVector2D(float(outputSize.width()), float(outputSize.height())));
    rcas->setUniform("strength", strength);
    if (!direct) {
        setColorUniforms(rcas, target);
    }
    draw(rcas, m_scaled.texture.get(), viewport, destination, region);
}

bool UpscaleScaler::renderTexture(const RenderTarget &target, const RenderViewport &viewport, GLTexture *input,
                                  const UpscaleDrawing &drawing, const UpscaleRegion &region)
{
    const UpscaleRectF &destination = drawing.destination;
    const bool sharpening = upscaleRcasFollows(drawing);
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
    const bool blending = glIsEnabled(GL_BLEND);
    glDisable(GL_BLEND);
    const auto restoreBlending = qScopeGuard([blending]() {
        if (blending) {
            glEnable(GL_BLEND);
        }
    });
    if (!drawing.frame.isEmpty()) {
        bars(viewport, drawing.frame, destination, region);
    }
    if (drawing.filter == UpscaleFilter::Nearest) {
        // The capture is already in the target's encoding and sampled nearest,
        // so this is a plain copy: each pixel replicated, nothing sharpened.
        m_scaled.release();
        ShaderBinder binder(ShaderTrait::MapTexture);
        draw(binder.shader(), input, viewport, destination, region);
        return true;
    }
    const QSize outputSize = (destination.size() * viewport.scale()).toSize();
    if (sharpening) {
        if (!m_scaled.resize(outputSize, upscaleFilterFormat(targetColors(target)))) {
            return false;
        }
    } else {
        // A destination-sized floating point image is the largest allocation
        // this effect makes. Switching sharpening off, or to a filter that
        // sharpens itself, has to return it.
        m_scaled.release();
    }

    // The input is opaque. Blending stays off, as set above, also where KWin's
    // item renderer enabled it for a buffer with an unused alpha channel.
    if (!scale(target, viewport, input, drawing, region)) {
        return false;
    }
    if (sharpening) {
        sharpen(target, viewport, destination, region, drawing.strength);
    }
    return true;
}

} // namespace KWin
