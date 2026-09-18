/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "overlay.h"

#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QScopeGuard>

#include <cmath>

namespace KWin
{

UpscaleOverlay::UpscaleOverlay() = default;
UpscaleOverlay::~UpscaleOverlay() = default;

// Text is measured and drawn at destination pixels rather than drawn small and
// enlarged, because the whole point of this overlay is to stay readable beside
// a game that is being enlarged.
static QImage renderText(const QString &text, double scale)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(std::max(11, int(std::lround(13 * scale))));
    const QFontMetricsF metrics(font);
    const QStringList lines = text.split(QLatin1Char('\n'));
    const double padding = std::round(8 * scale);
    const double lineHeight = std::ceil(metrics.height());
    double width = 0;
    for (const QString &line : lines) {
        width = std::max(width, metrics.horizontalAdvance(line));
    }
    const double textHeight = double(lines.size()) * lineHeight;
    const QSize size(int(std::ceil(width + (2 * padding))), int(std::ceil(textHeight + (2 * padding))));
    if (size.isEmpty()) {
        return QImage();
    }
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    // A translucent plate keeps the game visible underneath. KWin composites
    // premultiplied textures, which is what this format stores.
    image.fill(QColor(0, 0, 0, 190));
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255));
    painter.setRenderHint(QPainter::TextAntialiasing);
    double baseline = padding + metrics.ascent();
    for (const QString &line : lines) {
        painter.drawText(QPointF(padding, baseline), line);
        baseline += lineHeight;
    }
    painter.end();
    return image;
}

void UpscaleOverlay::setText(const QString &text, double scale)
{
    if (m_text == text && m_scale == scale) {
        return;
    }
    m_text = text;
    m_scale = scale;
    m_image = text.isEmpty() ? QImage() : renderText(text, scale);
    // The texture belongs to the old image. Uploading happens in the paint
    // pass, where a current OpenGL context is guaranteed.
    m_texture.reset();
}

QSizeF UpscaleOverlay::size() const
{
    return m_image.isNull() ? QSizeF() : QSizeF(m_image.size()) / m_scale;
}

QString UpscaleOverlay::text() const
{
    return m_image.isNull() ? QString() : m_text;
}

bool UpscaleOverlay::isEmpty() const
{
    return m_image.isNull();
}

void UpscaleOverlay::release()
{
    m_text.clear();
    m_image = QImage();
    m_texture.reset();
}

bool UpscaleOverlay::paint(const RenderTarget &target, const RenderViewport &viewport, const QPointF &position)
{
    if (m_image.isNull()) {
        return false;
    }
    if (!m_texture) {
        m_texture = GLTexture::upload(m_image);
        if (!m_texture) {
            return false;
        }
        m_texture->setFilter(GL_LINEAR);
        m_texture->setWrapMode(GL_CLAMP_TO_EDGE);
    }
    // The image is encoded as ordinary sRGB. Handing the conversion to KWin's
    // own colour shader keeps the text at the same brightness on an HDR output
    // instead of letting it glow at the display's peak luminance.
    GLShader *shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture | ShaderTrait::TransformColorspace);
    const auto pop = qScopeGuard([]() {
        ShaderManager::instance()->popShader();
    });
    // A diagnostic display must never be the reason the compositor stops. If
    // this shader cannot be had, the frame is drawn without the text.
    if (!validShader(shader)) {
        return false;
    }
    shader->setColorspaceUniforms(ColorDescription::sRGB, target.colorDescription(), RenderingIntent::Perceptual);
    QMatrix4x4 matrix = viewport.projectionMatrix();
    matrix.translate(float(std::round(position.x() * viewport.scale())), float(std::round(position.y() * viewport.scale())));
    shader->setUniform(GLShader::Mat4Uniform::ModelViewProjectionMatrix, matrix);
    shader->setUniform(GLShader::IntUniform::Sampler, 0);
    const bool blending = glIsEnabled(GL_BLEND);
    GLint sourceRgb, destinationRgb, sourceAlpha, destinationAlpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_texture->bind();
    m_texture->render(m_image.size());
    m_texture->unbind();
    glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
    if (!blending) {
        glDisable(GL_BLEND);
    }
    return true;
}

} // namespace KWin
