/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What each filter's program is made of, and what it is told before it draws.

#include "scaler.h"

#include "nisconfig.h"

#include "opengl/glshader.h"

#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

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
    case UpscaleFilter::Nis:
        return {QStringLiteral(":/effects/upscale/shaders/nis.frag"), QByteArray()};
    case UpscaleFilter::Xbrz:
        return {QStringLiteral(":/effects/upscale/shaders/xbrz.frag"), QByteArray()};
    }
    return {QStringLiteral(":/effects/upscale/shaders/upscale.frag"), QByteArray()};
}

// NIS's mode follows the destination's encoding: PQ is its own, a linear one
// its linear mode, and the others display-referred, as its SDR mode expects.
static UpscaleNisMode nisMode(const ColorDescription &colors)
{
    switch (colors.transferFunction().type) {
    case TransferFunction::linear:
        return UpscaleNisMode::Linear;
    case TransferFunction::PerceptualQuantizer:
        return UpscaleNisMode::Pq;
    default:
        return UpscaleNisMode::Sdr;
    }
}

static void setNisUniforms(GLShader *shader, double strength, const ColorDescription &colors)
{
    const UpscaleNisMode mode = nisMode(colors);
    const UpscaleNisConfig config = upscaleNisConfig(float(strength), mode);
    shader->setUniform("detection", QVector4D(config.detectRatio, config.detectThreshold, config.minContrastRatio, config.ratioNorm));
    shader->setUniform("sharpness", QVector4D(config.sharpStartY, config.sharpScaleY, config.sharpStrengthMin, config.sharpStrengthScale));
    shader->setUniform("sharpnessLimit", QVector2D(config.sharpLimitMin, config.sharpLimitScale));
    // getY's weights: BT.709's, and NVIDIA's own for PQ. A linear destination
    // takes BT.709's inside the program, as NIS_HDR_MODE_LINEAR does.
    shader->setUniform("lumaWeights", mode == UpscaleNisMode::Pq ? QVector3D(0.262F, 0.678F, 0.0593F) : QVector3D(0.2126F, 0.7152F, 0.0722F));
}

void upscaleSetFilterUniforms(GLShader *shader, const UpscaleDrawing &drawing, const QSize &input, const QSize &output,
                              const ColorDescription &colors, bool intermediate)
{
    shader->setUniform("inputSize", QVector2D(float(input.width()), float(input.height())));
    switch (drawing.filter) {
    case UpscaleFilter::Nearest:
    case UpscaleFilter::Bilinear:
    case UpscaleFilter::Bicubic:
    case UpscaleFilter::Lanczos:
        break;
    case UpscaleFilter::Fsr:
    case UpscaleFilter::Xbrz:
        shader->setUniform("outputSize", QVector2D(float(output.width()), float(output.height())));
        break;
    case UpscaleFilter::Gsr:
        // Qualcomm's edge sharpness runs from 1, its filter unsharpened, to 2,
        // its default.
        shader->setUniform("edgeSharpness", float(1.0 + drawing.strength));
        break;
    case UpscaleFilter::Nis:
        setNisUniforms(shader, drawing.strength, colors);
        break;
    }
    if (upscaleFilterSharpensWithRcas(drawing.filter)) {
        shader->setUniform("intermediate", int(intermediate));
    }
}

} // namespace KWin
