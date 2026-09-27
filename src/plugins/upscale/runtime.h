/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QSize>
#include <QString>
#include <QStringView>

#include <sys/types.h>

namespace KWin
{

// KWin reports the Unix loader behind a Wine/Proton window, not the Windows
// game's executable. Classifying the runtime does not authorize prefix access:
// the optional helper still proves ownership and obtains the user's consent.
inline bool upscaleWineRuntime(const QString &executable)
{
    const QStringView name = QStringView(executable).mid(executable.lastIndexOf(QLatin1Char('/')) + 1);
    return name == QLatin1String("wine") || name == QLatin1String("wine64")
        || name == QLatin1String("wine-preloader") || name == QLatin1String("wine64-preloader");
}

/**
 * Remember that @p pid was told its screen is @p screen, smaller than the
 * output it is shown on.
 *
 * Recorded when the transport's connection policy answers with a size, and
 * asked about again once that process has a window.
 */
void upscaleRecordServed(uint pid, const QSize &screen);

/**
 * Whether @p pid renders small because its connection was answered that way.
 *
 * A Wine program takes its screen from its prefix, which is settled before it
 * draws anything. One whose connection was answered already renders at the
 * size wanted and only has to be presented across its output; one whose was
 * not is at the size its prefix reports, and asking it to resize fights that
 * screen and flickers, so it is left alone.
 */
bool upscaleServed(pid_t pid);

/**
 * The screen @p pid was told it has, or an empty size for a process that was
 * told nothing.
 *
 * A program fills the screen it believes in, not the output: one in borderless
 * mode makes a window the size of this, and nothing about such a window says
 * fullscreen. Judging it against the output would find a window that covers
 * only part of it and leave a game the effect answered for unscaled.
 */
QSize upscaleServedScreen(pid_t pid);

}
