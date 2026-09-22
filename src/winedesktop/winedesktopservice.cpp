/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktopservice.h"
#include "winedesktophelper.h"

#include <QDBusMetaType>

QDBusArgument &operator<<(QDBusArgument &argument, const PreparedProgram &program)
{
    argument.beginStructure();
    argument << program.id << program.title << program.width << program.height;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PreparedProgram &program)
{
    argument.beginStructure();
    argument >> program.id >> program.title >> program.width >> program.height;
    argument.endStructure();
    return argument;
}

WineDesktopService::WineDesktopService(WineDesktopHelper *helper, QObject *parent)
    : QObject(parent)
    , m_helper(helper)
{
    qDBusRegisterMetaType<PreparedProgram>();
    qDBusRegisterMetaType<QList<PreparedProgram>>();
}

QString WineDesktopService::Offer(uint pid, const QString &windowClass, const QString &title, int width, int height, QString &question)
{
    const WineDesktopHelper::Offered offered = m_helper->offer(pid, windowClass, title, QSize(width, height));
    question = offered.question;
    return offered.offer;
}

QString WineDesktopService::Answer(const QString &offer, const QString &answer)
{
    return m_helper->answer(offer, answer);
}

bool WineDesktopService::Restart(const QString &offer)
{
    return m_helper->restart(offer);
}

int WineDesktopService::Present(uint pid, const QString &windowClass, int &height)
{
    const QSize size = m_helper->present(pid, windowClass);
    height = size.height();
    return size.width();
}

QList<PreparedProgram> WineDesktopService::Prepared()
{
    QList<PreparedProgram> programs;
    const QList<WineDesktopRecord> records = m_helper->prepared();
    for (const WineDesktopRecord &record : records) {
        programs.append({.id = record.id, .title = record.title, .width = record.written->width(), .height = record.written->height()});
    }
    return programs;
}

bool WineDesktopService::Reset(const QString &id)
{
    return m_helper->reset(id);
}
