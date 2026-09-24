/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "winescreenwrite.h"

#include <QList>
#include <QSize>
#include <QString>

#include <optional>

/*
 * What the companion remembers about each prefix it was asked about, so that
 * it only ever undoes its own change and never asks again after "never". It
 * lives in the user's state directory, not in the prefix.
 */
struct WineScreenRecord
{
    // The prefix's device and inode in hexadecimal; the same prefix keeps it
    // whatever path leads to it.
    QString id;
    // The window title the user saw, for the settings page.
    QString title;
    WineScreenTarget target;
    QString steamAppId;
    // The size wanted, and the size this companion wrote into the prefix if it
    // did; they differ while a change waits for the game to exit.
    QSize wanted;
    std::optional<QSize> written;
    // The screens as they were described, in one line (wineScreensText): a
    // layout the outputs no longer have - another rate, a screen plugged or
    // unplugged - is described again, so that what a program sees stays what
    // the session has.
    QString described;
    bool never = false;
};

QString wineRecordId(const WinePrefixIdentity &identity);

class WineScreenRecords
{
public:
    explicit WineScreenRecords(QString path);

    QList<WineScreenRecord> all() const;
    std::optional<WineScreenRecord> find(const QString &id) const;
    void store(const WineScreenRecord &record);
    void remove(const QString &id);

private:
    QString m_path;
};
