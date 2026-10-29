/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What each filter's program is made of, and what it is told before it draws.

#include "scaler.h"

#include "opengl/glshader.h"

#include <QVector2D>

namespace KWin
{

// The three classic filters are one program, with the kernel chosen when it
// is compiled.
std::pair<QString, QByteArray> upscaleFilterSource(UpscaleFilter filter)
{
    const QString classic = QStringLiteral(":/effects/upscale/shaders/classic.frag");
    switch (filter) {
    case UpscaleFilter::Fsr:
    case UpscaleFilter::Nearest:
        break;
    case UpscaleFilter::Bilinear:
        return {classic, QByteArrayLiteral("#define UPSCALE_KERNEL 0\n")};
    case UpscaleFilter::Bicubic:
        return {classic, QByteArrayLiteral("#define UPSCALE_KERNEL 1\n")};
    case UpscaleFilter::Lanczos:
        return {classic, QByteArrayLiteral("#define UPSCALE_KERNEL 2\n")};
    case UpscaleFilter::Gsr:
        return {QStringLiteral(":/effects/upscale/shaders/gsr.frag"), QByteArray()};
    }
    return {QStringLiteral(":/effects/upscale/shaders/upscale.frag"), QByteArray()};
}

void upscaleSetFilterUniforms(GLShader *shader, const UpscaleDrawing &drawing, const QSize &input, const QSize &output, bool intermediate)
{
    shader->setUniform("inputSize", QVector2D(float(input.width()), float(input.height())));
    switch (drawing.filter) {
    case UpscaleFilter::Nearest:
    case UpscaleFilter::Bilinear:
    case UpscaleFilter::Bicubic:
    case UpscaleFilter::Lanczos:
        break;
    case UpscaleFilter::Fsr:
        shader->setUniform("outputSize", QVector2D(float(output.width()), float(output.height())));
        break;
    case UpscaleFilter::Gsr:
        // Qualcomm's edge sharpness runs from 1, its filter unsharpened, to 2,
        // its default.
        shader->setUniform("edgeSharpness", float(1.0 + drawing.strength));
        break;
    }
    if (upscaleFilterSharpensWithRcas(drawing.filter)) {
        shader->setUniform("intermediate", int(intermediate));
    }
}

} // namespace KWin
