/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The filters' names, which the settings page lists and the effect's status
// and heads-up display repeat, so that both say the same thing.

#pragma once

#include "picture.h"

#include <KLocalizedString>

#include <QString>

namespace KWin
{

/** The name of @p filter, as a list or the start of a line shows it. */
inline QString upscaleFilterName(UpscaleFilter filter)
{
    switch (filter) {
    case UpscaleFilter::Fsr:
        return i18nc("Scaling filter", "FSR 1");
    case UpscaleFilter::Nearest:
        return i18nc("Scaling filter", "Nearest neighbor");
    case UpscaleFilter::Bilinear:
        return i18nc("Scaling filter", "Bilinear");
    case UpscaleFilter::Bicubic:
        return i18nc("Scaling filter", "Bicubic");
    case UpscaleFilter::Lanczos:
        return i18nc("Scaling filter", "Lanczos");
    case UpscaleFilter::Gsr:
        return i18nc("Scaling filter", "Snapdragon GSR 1");
    case UpscaleFilter::Nis:
        return i18nc("Scaling filter", "NVIDIA Image Scaling");
    }
    return QString();
}

/**
 * What @p filter did with a buffer, sharpened by @p sharpening, as a clause
 * of the status line after the sizes. The sharpener is named, because RCAS
 * after a filter and a filter's own sharpening are different things.
 */
inline QString upscaleFilterClause(UpscaleFilter filter, double sharpening)
{
    QString name;
    switch (filter) {
    case UpscaleFilter::Fsr:
        name = i18nc("Scaling filter, within a sentence", "FSR 1");
        break;
    case UpscaleFilter::Nearest:
        return i18nc("Scaling filter, within a sentence", "nearest neighbor");
    case UpscaleFilter::Bilinear:
        name = i18nc("Scaling filter, within a sentence", "bilinear");
        break;
    case UpscaleFilter::Bicubic:
        name = i18nc("Scaling filter, within a sentence", "bicubic");
        break;
    case UpscaleFilter::Lanczos:
        name = i18nc("Scaling filter, within a sentence", "Lanczos");
        break;
    case UpscaleFilter::Gsr:
        name = i18nc("Scaling filter, within a sentence", "Snapdragon GSR 1");
        break;
    case UpscaleFilter::Nis:
        name = i18nc("Scaling filter, within a sentence", "NVIDIA Image Scaling");
        break;
    }
    const int percent = qRound(sharpening * 100);
    if (percent <= 0) {
        return i18nc("What the effect does with the buffer, after the sizes or a window's states", "%1, no sharpening", name);
    }
    return upscaleFilterSharpensWithRcas(filter)
        ? i18nc("What the effect does with the buffer, after the sizes or a window's states", "%1 with RCAS sharpening %2%", name, percent)
        : i18nc("What the effect does with the buffer, after the sizes or a window's states", "%1 with its own sharpening %2%", name, percent);
}

} // namespace KWin
