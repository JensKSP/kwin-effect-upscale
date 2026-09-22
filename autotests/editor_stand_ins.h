/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusContext>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// KWin picks the window itself and hands back its identity. Only this private
// bus answers here, and each case the settings page has to handle is selected
// by the reply this stands in for.
class TestWindowPicker : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")

public:
    enum Outcome {
        Identified,
        WithoutIdentity,
        Cancelled,
        Refused,
    };
    Outcome outcome = Identified;

public Q_SLOTS:
    QVariantMap queryWindowInfo()
    {
        if (outcome == Cancelled) {
            sendErrorReply(QStringLiteral("org.kde.KWin.Error.UserCancel"), QStringLiteral("Cancelled"));
            return {};
        }
        if (outcome == Refused) {
            sendErrorReply(QStringLiteral("org.kde.KWin.Error.InvalidWindow"), QStringLiteral("No such window"));
            return {};
        }
        if (outcome == WithoutIdentity) {
            return {{QStringLiteral("resourceClass"), QString()}, {QStringLiteral("resourceName"), QString()}};
        }
        return {{QStringLiteral("resourceClass"), QStringLiteral("hedgewars")},
                {QStringLiteral("resourceName"), QStringLiteral("hedgewars")},
                {QStringLiteral("uuid"), QStringLiteral("{0b4a6c3e-8f0e-4a55-9d2c-1d1f5e3b7a90}")}};
    }
};

// The effect, which answers the page's question which program a window
// belongs to. KWin's picker does not say in KWin 6.3.
class TestProgramLookup : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Effect.Upscale1")

public:
    QString path;
    QString askedFor;
    QStringList windows;
    QVariantMap entry;

public Q_SLOTS:
    QString executablePath(const QString &window)
    {
        askedFor = window;
        return path;
    }

    QStringList windowsMatching(const QVariantMap &asked)
    {
        entry = asked;
        return windows;
    }
};
