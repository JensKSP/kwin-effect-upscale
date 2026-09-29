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
struct UpscaleApplication;

// KWin reports the Unix loader behind a Wine/Proton window, not the Windows
// game's executable. Wine takes its screen from its prefix when it starts, so
// such a window is acted on only where the X11 proxy answered its connection;
// see upscaleServed().
inline bool upscaleWineRuntime(const QString &executable)
{
    const QStringView name = QStringView(executable).mid(executable.lastIndexOf(QLatin1Char('/')) + 1);
    return name == QLatin1String("wine") || name == QLatin1String("wine64")
        || name == QLatin1String("wine-preloader") || name == QLatin1String("wine64-preloader");
}

/**
 * Remember that @p pid was told its screen is @p screen, smaller than the
 * output it is shown on, by the entry with the id @p profile, or by the global
 * profile where that is empty.
 *
 * Recorded when the transport's connection policy answers with a size, and
 * asked about again once that process has a window.
 */
void upscaleRecordServed(uint pid, const QSize &screen, const QString &profile = QString());

/**
 * Remember that @p pid was shown the screen @p game was served, as one of the
 * processes of the Wine prefix that game runs in: one prefix is one screen.
 * Nothing is recorded where @p game was not served.
 */
void upscaleRecordShown(uint game, uint pid);

/**
 * The entry that answered for @p pid's screen, where it is still enabled.
 *
 * A window no entry names of a process answered for one, a Wine prefix's
 * launcher beside its game for instance, is claimed by that entry: it renders
 * at the screen that entry wanted, so it is presented as that entry's windows
 * are, as Jens decided on 2026-09-29. Null for a process the global
 * profile answered for, or none did.
 */
const UpscaleApplication *upscaleServedApplication(pid_t pid);

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
