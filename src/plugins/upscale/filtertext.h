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
    }
    return QString();
}

/**
 * What @p filter did with a buffer, sharpened by @p sharpening, as a clause
 * of the status line after the sizes, naming the sharpener.
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
    }
    const int percent = qRound(sharpening * 100);
    if (percent <= 0) {
        return i18nc("What the effect does with the buffer, after the sizes or a window's states", "%1, no sharpening", name);
    }
    return i18nc("What the effect does with the buffer, after the sizes or a window's states", "%1 with RCAS sharpening %2%", name, percent);
}

} // namespace KWin
