/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QSize>

#include <optional>

/*
 * The display modes a Wine prefix keeps in its registry, in the binary shape
 * win32u writes and reads there: DEVMODEW records of 220 bytes, the list in the
 * Modes value and one mode each in Current and Registry, the latter two from the
 * record's dmFields member on (dlls/win32u/sysparams.c, add_modes,
 * read_source_from_registry and read_source_mode).
 *
 * A record only ever describes a size, a colour depth and a refresh rate here;
 * everything else in it stays zero, as it does in the modes Wine generates for
 * a prefix itself.
 */

/*
 * The modes a screen of that size offers: that size alone, in the colour depths
 * Wine offers and at 60 Hz and the screen's own rate. Empty for an empty size.
 *
 * Wine matches a mode a program asks for against this list and refuses one that
 * is not in it, so a list of one size is a screen a program cannot choose
 * another size on. That is the point: a game offered several sizes picks one of
 * them, and which one it picks is the game's own business - Wreckfest takes the
 * smallest (measured 2026-09-23) - while the size a program is to render at is
 * the effect's to decide.
 */
QByteArray wineDisplayModes(const QSize &size, int refreshRate);

/*
 * How many modes such a list holds.
 */
int wineDisplayModeCount(const QByteArray &modes);

/*
 * One mode as the Current and Registry values hold it. A rate of zero leaves
 * the rate unsaid, which is how Wine itself stores the current mode.
 */
QByteArray wineDisplayMode(const QSize &size, int refreshRate);

/*
 * The size in such a value, and nothing when the data is not one mode in that
 * shape.
 */
std::optional<QSize> wineDisplayModeSize(const QByteArray &value);
