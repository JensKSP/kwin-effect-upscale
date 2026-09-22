/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include <optional>

/*
 * The two values in a Wine prefix's user.reg that give its programs a virtual
 * desktop, and therefore a monitor of that desktop's size:
 *
 *   [Software\\Wine\\Explorer]            "Desktop"="Default"
 *   [Software\\Wine\\Explorer\\Desktops]  "Default"="<width>x<height>"
 *
 * The desktop has to be called Default: win32u takes the virtual monitor's
 * size from Desktops\Default only (dlls/win32u/sysparams.c,
 * get_default_desktop_size), whatever the desktop's name.
 *
 * These functions only transform the file's text. They change the lines of
 * those two values and nothing else, so every other byte of the file stays as
 * Wine wrote it. When and whether the file may be written is decided elsewhere:
 * the Wine server rewrites user.reg while it runs, so an edit is only safe while
 * no server holds the prefix.
 */

struct WineDesktopValues
{
    std::optional<QString> desktop;
    std::optional<QString> defaultSize;

    bool operator==(const WineDesktopValues &other) const = default;
};

/*
 * Whether the text is a registry file this code understands: Wine's own header
 * on the first line.
 */
bool wineIsRegistry(const QByteArray &text);

/*
 * The two values as the file holds them. Values of another type than a plain
 * string are reported as absent.
 */
WineDesktopValues wineDesktopValues(const QByteArray &text);

/*
 * The file with a virtual desktop of the given size. A key that is missing is
 * added at the end with the given modification time, in seconds since 1970;
 * a value that exists is replaced in place. Returns nothing for text that is
 * not a registry file or for an empty size.
 */
std::optional<QByteArray> wineWithVirtualDesktop(const QByteArray &text, const QSize &size, qint64 modifiedSeconds);

/*
 * The file without the two values. The keys themselves stay, as winecfg leaves
 * them. Returns nothing for text that is not a registry file.
 */
std::optional<QByteArray> wineWithoutVirtualDesktop(const QByteArray &text);
