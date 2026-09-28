/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QChar>
#include <QString>

namespace KWin
{

/**
 * The marks around a part of an on-screen display text that the overlay draws
 * in its warning colour: a value that is not what was asked for, such as the
 * size a game draws at when it did not take the one chosen for it.
 *
 * Two characters from Unicode's private use area, which no translation
 * contains and which the overlay removes before measuring or drawing. A text
 * that reaches anything but the overlay carries no marks at all.
 */
inline constexpr QChar upscaleWarningStart{u''};
inline constexpr QChar upscaleWarningEnd{u''};

/** @p text, marked to be drawn in the warning colour. */
inline QString upscaleWarning(const QString &text)
{
    return QString(upscaleWarningStart) + text + QString(upscaleWarningEnd);
}

/**
 * The marks around a part drawn in the highlight colour instead: the answer
 * that is selected in a question, the way KDE highlights a focused choice.
 * The same private use area, removed the same way.
 */
inline constexpr QChar upscaleHighlightStart{u'\uE002'};
inline constexpr QChar upscaleHighlightEnd{u'\uE003'};

/** @p text, marked to be drawn in the highlight colour. */
inline QString upscaleHighlight(const QString &text)
{
    return QString(upscaleHighlightStart) + text + QString(upscaleHighlightEnd);
}

} // namespace KWin
