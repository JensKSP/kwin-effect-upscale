/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What the texts made of a snapshot share: snapshot.cpp's status and summary
// and snapshot_headsup.cpp's heads-up display.

#pragma once

#include "snapshot.h"
#include "warningtext.h"

#include <KLocalizedString>

#include <QLocale>
#include <QSize>
#include <QString>

namespace KWin
{

inline QString upscaleUnknownText()
{
    return i18nc("A value the effect cannot observe", "unknown");
}

inline QString upscaleSizeText(const QSize &size)
{
    // Pixel counts are substituted as text on purpose. Passing the integers
    // lets the locale group them, and "3.840 × 2.160" reads as two fractional
    // numbers rather than as a resolution.
    return size.isEmpty() ? upscaleUnknownText()
                          : i18n("%1 × %2", QString::number(size.width()), QString::number(size.height()));
}

inline QString upscaleFigureText(double value, int decimals)
{
    // A measured figure in the reader's own notation: a German session reads
    // 59,9 where the source language reads 59.9. Never grouped, so that a
    // fractional coordinate reads like the pixel sizes beside it.
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value, 'f', decimals);
}

// The size the game draws at, marked for the warning colour where it is not
// the one chosen - a game that keeps a resolution of its own, say. Only for the
// on-screen display, whose overlay understands the marks.
inline QString upscaleSuppliedForDisplay(const UpscaleSnapshot &snapshot, const QString &name)
{
    return upscaleDrawsTheChosenSize(snapshot) ? name : upscaleWarning(name);
}

} // namespace KWin
