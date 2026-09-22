/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "overlay.h"

#include "warningtext.h"

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

// The session states a font and a size; the screen states how many pixels a
// device-independent one is worth. Both apply: this is KDE user interface, so
// it is the size the person chose for their desktop, at the scale factor that
// screen was configured with, and it changes when either of them does.
//
// A point is a seventy-second of an inch and Qt's device-independent pixel a
// ninety-sixth, which is the reference every KDE scale factor is stated
// against. A font that states its size in pixels already speaks in those.
static QFont displayFont(double scale)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const double points = font.pointSizeF();
    const double pixels = points > 0 ? points * (96.0 / 72.0) : double(font.pixelSize());
    // A size nothing could be read at is not worth drawing; below this the
    // configured size is ignored rather than the text being lost.
    font.setPixelSize(std::max(11, int(std::lround(pixels * scale))));
    return font;
}

// The colour a marked part of a line is drawn in: Breeze's negative text,
// which is how KDE shows a value that is wrong. The overlay is drawn inside
// the compositor with no palette of its own to take it from.
static const QColor s_warningColor(0xda, 0x44, 0x53);
// Breeze's highlight, for the selected answer of a question.
static const QColor s_highlightColor(0x3d, 0xae, 0xe9);

static QString withoutWarningMarks(QString line)
{
    for (const QChar mark : {upscaleWarningStart, upscaleWarningEnd, upscaleHighlightStart, upscaleHighlightEnd}) {
        line.remove(mark);
    }
    return line;
}

// One line, white, with any part between marks in the colour they name.
static void drawLine(QPainter &painter, const QFontMetricsF &metrics, QPointF origin, const QString &line)
{
    QString run;
    QColor colour(255, 255, 255);
    const auto flush = [&]() {
        if (run.isEmpty()) {
            return;
        }
        painter.setPen(colour);
        painter.drawText(origin, run);
        origin.rx() += metrics.horizontalAdvance(run);
        run.clear();
    };
    for (const QChar character : line) {
        if (character == upscaleWarningStart || character == upscaleHighlightStart) {
            flush();
            colour = character == upscaleWarningStart ? s_warningColor : s_highlightColor;
        } else if (character == upscaleWarningEnd || character == upscaleHighlightEnd) {
            flush();
            colour = QColor(255, 255, 255);
        } else {
            run.append(character);
        }
    }
    flush();
}

// Text is measured and drawn at destination pixels rather than drawn small and
// enlarged, because the whole point of this overlay is to stay readable beside
// a game that is being enlarged.
static QImage renderText(const QString &text, double scale)
{
    const QFont font = displayFont(scale);
    const QFontMetricsF metrics(font);
    const QStringList lines = text.split(QLatin1Char('\n'));
    const double padding = std::round(8 * scale);
    const double lineHeight = std::ceil(metrics.height());
    double width = 0;
    for (const QString &line : lines) {
        width = std::max(width, metrics.horizontalAdvance(withoutWarningMarks(line)));
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
    painter.setRenderHint(QPainter::TextAntialiasing);
    double baseline = padding + metrics.ascent();
    for (const QString &line : lines) {
        drawLine(painter, metrics, QPointF(padding, baseline), line);
        baseline += lineHeight;
    }
    painter.end();
    return image;
}

// Lay the text out at the size it asked for, and again smaller when that does
// not fit the quarter of the screen it was given.
//
// One retry is enough. Text metrics follow the font size closely but not
// exactly — hinting and integer pixel sizes move the result by a fraction of a
// character — so a second pass would chase a bound it has already very nearly
// reached. The crop below is what makes the bound exact, and it removes a few
// pixels rather than a word.
//
// The smallest readable font size is a floor inside renderText(), so a block
// on an output with almost no room stops shrinking while it can still be read
// and is cropped instead. Losing the end of a line beats losing the line.
static QImage renderFitted(const QString &text, double factor, double scale, const QSizeF &budget)
{
    QImage image = renderText(text, factor);
    if (image.isNull() || !budget.isValid()) {
        return image;
    }
    const QSizeF logical = QSizeF(image.size()) / scale;
    if (logical.width() > budget.width() || logical.height() > budget.height()) {
        const double fit = std::min(budget.width() / logical.width(), budget.height() / logical.height());
        image = renderText(text, factor * fit);
    }
    // The budget is in the output's coordinates and the image in destination
    // pixels, which is what the block is measured and drawn in.
    const QSize allowed(int(std::floor(budget.width() * scale)), int(std::floor(budget.height() * scale)));
    // No room is an answer of its own: nothing is drawn. It cannot be left to
    // the crop below, because a rectangle of no size is a null rectangle and
    // QImage::copy() copies the whole image for one, which would draw the
    // block at full size into a corner that has none to give.
    if (allowed.isEmpty()) {
        return QImage();
    }
    if (image.width() > allowed.width() || image.height() > allowed.height()) {
        image = image.copy(QRect(QPoint(), image.size().boundedTo(allowed)));
    }
    return image;
}

void UpscaleOverlay::setText(const QString &text, double scale, double emphasis)
{
    if (m_text == text && m_scale == scale && m_emphasis == emphasis) {
        return;
    }
    m_text = text;
    m_scale = scale;
    m_emphasis = emphasis;
    // What was laid out described the old text. Laying the new one out waits
    // for fit(), which is where the room it has to fit into is known.
    m_fitted = false;
    m_image = QImage();
    m_texture.reset();
}

void UpscaleOverlay::fit(const QSizeF &budget)
{
    if (m_fitted && m_budget == budget) {
        return;
    }
    m_budget = budget;
    m_fitted = false;
    // The texture belongs to the old image. Uploading happens in the paint
    // pass, where a current OpenGL context is guaranteed.
    m_texture.reset();
}

void UpscaleOverlay::layOut() const
{
    if (m_fitted) {
        return;
    }
    m_fitted = true;
    // The emphasis enlarges what is drawn; the logical size this reports still
    // divides by the screen's own scale, so placement stays in the output's
    // coordinates and a larger block simply occupies more of them.
    m_image = m_text.isEmpty() ? QImage() : renderFitted(m_text, m_scale * m_emphasis, m_scale, m_budget);
}

QSizeF UpscaleOverlay::size() const
{
    layOut();
    return m_image.isNull() ? QSizeF() : QSizeF(m_image.size()) / m_scale;
}

QString UpscaleOverlay::text() const
{
    return m_text;
}

bool UpscaleOverlay::isEmpty() const
{
    return m_text.isEmpty();
}

void UpscaleOverlay::release()
{
    m_text.clear();
    m_budget = QSizeF();
    m_fitted = false;
    m_image = QImage();
    m_texture.reset();
}

bool UpscaleOverlay::paint(const RenderTarget &target, const RenderViewport &viewport, const QPointF &position)
{
    layOut();
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
