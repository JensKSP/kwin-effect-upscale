/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktoprecord.h"

#include <QSettings>

namespace
{

WineDesktopRecord readRecord(QSettings &settings, const QString &id)
{
    settings.beginGroup(id);
    WineDesktopRecord record{
        .id = id,
        .title = settings.value(QStringLiteral("Title")).toString(),
        .target =
            {
                .prefix = settings.value(QStringLiteral("Prefix")).toString(),
                .identity =
                    {
                        .device = static_cast<dev_t>(settings.value(QStringLiteral("Device")).toULongLong()),
                        .inode = static_cast<ino_t>(settings.value(QStringLiteral("Inode")).toULongLong()),
                    },
                .steamCompatData = settings.value(QStringLiteral("SteamCompatData")).toString(),
                .temporaryDirectory = settings.value(QStringLiteral("TemporaryDirectory")).toString(),
                .directory = nullptr,
            },
        .steamAppId = settings.value(QStringLiteral("SteamAppId")).toString(),
        .wanted = settings.value(QStringLiteral("Wanted")).toSize(),
        .written = std::nullopt,
        .never = settings.value(QStringLiteral("Never"), false).toBool(),
    };
    if (settings.contains(QStringLiteral("Written"))) {
        record.written = settings.value(QStringLiteral("Written")).toSize();
    }
    settings.endGroup();
    return record;
}

} // namespace

QString wineRecordId(const WinePrefixIdentity &identity)
{
    return QStringLiteral("%1-%2").arg(static_cast<qulonglong>(identity.device), 0, 16).arg(static_cast<qulonglong>(identity.inode), 0, 16);
}

WineDesktopRecords::WineDesktopRecords(QString path)
    : m_path(std::move(path))
{
}

QList<WineDesktopRecord> WineDesktopRecords::all() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    QList<WineDesktopRecord> records;
    const QStringList ids = settings.childGroups();
    for (const QString &id : ids) {
        records.append(readRecord(settings, id));
    }
    return records;
}

std::optional<WineDesktopRecord> WineDesktopRecords::find(const QString &id) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    if (!settings.childGroups().contains(id)) {
        return std::nullopt;
    }
    return readRecord(settings, id);
}

void WineDesktopRecords::store(const WineDesktopRecord &record)
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.remove(record.id);
    settings.beginGroup(record.id);
    settings.setValue(QStringLiteral("Title"), record.title);
    settings.setValue(QStringLiteral("Prefix"), record.target.prefix);
    settings.setValue(QStringLiteral("Device"), static_cast<qulonglong>(record.target.identity.device));
    settings.setValue(QStringLiteral("Inode"), static_cast<qulonglong>(record.target.identity.inode));
    settings.setValue(QStringLiteral("SteamCompatData"), record.target.steamCompatData);
    settings.setValue(QStringLiteral("TemporaryDirectory"), record.target.temporaryDirectory);
    settings.setValue(QStringLiteral("SteamAppId"), record.steamAppId);
    settings.setValue(QStringLiteral("Wanted"), record.wanted);
    if (record.written) {
        settings.setValue(QStringLiteral("Written"), *record.written);
    }
    settings.setValue(QStringLiteral("Never"), record.never);
    settings.endGroup();
}

void WineDesktopRecords::remove(const QString &id)
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.remove(id);
}
