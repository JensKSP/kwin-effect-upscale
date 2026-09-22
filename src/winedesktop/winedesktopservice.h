/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QString>

class WineDesktopHelper;

// One entry of Prepared(): identifier, title, width, height, as a(ssii).
struct PreparedProgram
{
    QString id;
    QString title;
    int width = 0;
    int height = 0;
};
Q_DECLARE_METATYPE(PreparedProgram)

QDBusArgument &operator<<(QDBusArgument &argument, const PreparedProgram &program);
const QDBusArgument &operator>>(const QDBusArgument &argument, PreparedProgram &program);

/*
 * org.kde.KWin.Upscale.Helper1 on the session bus, as specified in the
 * plugin's org.kde.KWin.Upscale.Helper1.xml, answered by the Wine desktop
 * helper.
 */
class WineDesktopService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Upscale.Helper1")

public:
    explicit WineDesktopService(WineDesktopHelper *helper, QObject *parent = nullptr);

public Q_SLOTS:
    Q_SCRIPTABLE QString Offer(uint pid, const QString &windowClass, const QString &title, int width, int height, QString &question);
    Q_SCRIPTABLE QString Answer(const QString &offer, const QString &answer);
    Q_SCRIPTABLE bool Restart(const QString &offer);
    Q_SCRIPTABLE int Present(uint pid, const QString &windowClass, int &height);
    Q_SCRIPTABLE QList<PreparedProgram> Prepared();
    Q_SCRIPTABLE bool Reset(const QString &id);

private:
    WineDesktopHelper *m_helper;
};
