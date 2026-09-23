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
 * What this companion reads and writes in a Wine prefix's registry files.
 *
 * In system.reg it describes the screen the prefix's programs see. Wine keeps
 * that description in the prefix itself and reads it before it asks the display
 * server: the size of the monitor, and the list of modes a program may choose
 * from. It normally writes it anew for every session, in keys that live only as
 * long as the Wine server, and reaches them through a key that is only a link
 * (dlls/win32u/sysparams.c, write_source_to_registry). A key of our own where
 * that link would go is read instead, and is read again after every refresh, so
 * the prefix keeps the screen we describe until the description is removed:
 *
 *   [HARDWARE\\DEVICEMAP\\VIDEO]               "\\Device\\Video0"=the source key
 *   [...\\Hardware Profiles\\Current\\...]     the size, the modes, the devices
 *
 * The source key says which graphics card and monitor the screen belongs to.
 * Those two the prefix has described itself, in keys that outlast a session,
 * since the first time it ran; without them there is nothing to write.
 *
 * In user.reg it reads the two values that give a prefix a virtual desktop. The
 * companion does not write them: a desktop is the user's own setting, and a
 * prefix that has one is left alone.
 *
 * These functions only transform the files' text. They change the lines they are
 * about and nothing else, so every other byte stays as Wine wrote it. When and
 * whether a file may be written is decided elsewhere: the Wine server rewrites
 * both files while it runs, so an edit is only safe while no server holds the
 * prefix.
 */

struct WineDesktopValues
{
    std::optional<QString> desktop;
    std::optional<QString> defaultSize;

    bool operator==(const WineDesktopValues &other) const = default;
};

/*
 * The graphics card and monitor a prefix has described for itself, as its
 * registry spells them, backslashes doubled. Empty when the prefix has not
 * described them yet, which is the case until its first program has run.
 */
struct WineScreenDevices
{
    QByteArray card;
    QByteArray cardId;
    QByteArray monitor;

    bool isEmpty() const;
    bool operator==(const WineScreenDevices &other) const = default;
};

/*
 * Whether the text is a registry file this code understands: Wine's own header
 * on the first line.
 */
bool wineIsRegistry(const QByteArray &text);

/*
 * The two virtual desktop values as user.reg holds them. Values of another type
 * than a plain string are reported as absent.
 */
WineDesktopValues wineDesktopValues(const QByteArray &text);

/*
 * The devices in system.reg the screen would belong to.
 */
WineScreenDevices wineScreenDevices(const QByteArray &text);

/*
 * The size of the screen described in system.reg, and nothing when none is.
 */
std::optional<QSize> wineScreen(const QByteArray &text);

/*
 * system.reg describing a screen of that size, running at that rate, in place of
 * any screen described before. A key that is missing is added at the end with
 * the given modification time, in seconds since 1970. Returns nothing for text
 * that is not a registry file, for an empty size, or for a prefix that has not
 * described its devices yet.
 */
std::optional<QByteArray> wineWithScreen(const QByteArray &text, const QSize &size, int refreshRate, qint64 modifiedSeconds);

/*
 * system.reg without the screen this companion described, so that Wine asks the
 * display server again. Returns nothing for text that is not a registry file.
 */
std::optional<QByteArray> wineWithoutScreen(const QByteArray &text);
