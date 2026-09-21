/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wayland_client.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QSocketNotifier>
#include <QTest>

#include <optional>

class UpscaleIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void lifecycle();
    void asksApplicationsForASmallerImage();
    void selectedBorderlessPresentation();
    void outputPixelPolicy();

private:
    // The global resolution as kwinrc stores it. Spelled out here rather than
    // taken from the plugin, because this test drives the effect from the
    // outside and the stored number is the contract: if the enumeration is
    // ever renumbered again, this is where that has to show up.
    enum class Stored {
        Native = 0,
        Quality = 2,
        Balanced = 3,
        Performance = 4,
    };
    QString status();
    void configure(bool unlisted, bool sharpening, std::optional<Stored> resolution = {});
    void configureColors(bool unsupported);
    void configureDisplay(bool enabled, bool statistics);
    void configureResolution(bool control, bool unlisted, std::optional<Stored> resolution);
    void writeCatalogue(const QString &contents);
    void reconfigure();
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};

// The catalogue entry every case describes the test client by, with whatever
// the case needs appended: its program, which is what finds it before it has a
// window and after.
QString integrationEntry(const QString &rest);

// An entry naming the window alone, for the cases about a window only.
QString integrationWindow(const QString &rest);
