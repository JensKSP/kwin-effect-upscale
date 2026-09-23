/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

class WineDesktopHelper;

// One entry of prepared(): identifier, title, width, height, as a(ssii).
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

// One screen a program is to see: where it lies, how large, and how often it
// refreshes, as a(iiiii). The first of them is the screen the program is on,
// at the size the effect wants it to render at.
struct ProgramScreen
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int rate = 0;
};
Q_DECLARE_METATYPE(ProgramScreen)

QDBusArgument &operator<<(QDBusArgument &argument, const ProgramScreen &screen);
const QDBusArgument &operator>>(const QDBusArgument &argument, ProgramScreen &screen);

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
    Q_SCRIPTABLE QString offer(uint pid, const QString &windowClass, const QString &title, const QList<ProgramScreen> &screens,
                               QString &question);
    Q_SCRIPTABLE QString answer(const QString &offer, const QString &answer);
    Q_SCRIPTABLE bool restart(const QString &offer);
    Q_SCRIPTABLE QSize present(uint pid, const QString &windowClass, const QList<ProgramScreen> &wanted);
    Q_SCRIPTABLE QList<PreparedProgram> prepared();
    Q_SCRIPTABLE bool reset(const QString &id);

private:
    WineDesktopHelper *m_helper;
};
