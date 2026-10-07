/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace KWin
{

/**
 * Whether the program named @p program, as upscaleProgramName() or the X11
 * session proxy names it, is recognized as a game.
 *
 * The global profile, All games, acts only for these; an entry acts for the
 * program it names whatever this says. A game is a program that runs under
 * Wine or Proton, one under a Steam library's steamapps/common, and one an
 * installed desktop entry in the Game category starts: by its Exec program, by
 * the Flatpak application of a flatpak:// name, or by the snap of a /snap/
 * path. A program an entry starts through an interpreter or a shell is not
 * told apart from the interpreter's other programs, so it is recognized only
 * by its window; see upscaleGameDesktopFile().
 *
 * The entries are read when this is first asked, and again once one of them
 * or a directory they were read from changed, so a game installed while the
 * session runs is recognized from its next start. That costs a look at each
 * entry's time, so this is asked only while All games acts: when a program
 * connects and when its window is first seen, never per frame.
 */
bool upscaleRecognizedGame(const QString &program);

/**
 * Whether the desktop entry a window names, @p desktopFileName as KWin reports
 * it, is in the Game category. This recognizes a game started through a
 * script or an interpreter, whose process names no program of its own.
 */
bool upscaleGameDesktopFile(const QString &desktopFileName);

} // namespace KWin
