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
    // The signature QtDBus requires of a demarshaller returns its argument.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return argument;
}

WineDesktopService::WineDesktopService(WineDesktopHelper *helper, QObject *parent)
    : QObject(parent)
    , m_helper(helper)
{
    qDBusRegisterMetaType<PreparedProgram>();
    qDBusRegisterMetaType<QList<PreparedProgram>>();
}

QString WineDesktopService::offer(uint pid, const QString &windowClass, const QString &title, int width, int height, QString &question)
{
    const WineDesktopHelper::Offered offered = m_helper->offer(pid, windowClass, title, QSize(width, height));
    question = offered.question;
    return offered.offer;
}

QString WineDesktopService::answer(const QString &offer, const QString &answer)
{
    return m_helper->answer(offer, answer);
}

bool WineDesktopService::restart(const QString &offer)
{
    return m_helper->restart(offer);
}

int WineDesktopService::present(uint pid, const QString &windowClass, int &height)
{
    const QSize size = m_helper->present(pid, windowClass);
    height = size.height();
    return size.width();
}

QList<PreparedProgram> WineDesktopService::prepared()
{
    QList<PreparedProgram> programs;
    const QList<WineDesktopRecord> records = m_helper->prepared();
    for (const WineDesktopRecord &record : records) {
        const QSize size = record.written.value_or(QSize());
        programs.append({.id = record.id, .title = record.title, .width = size.width(), .height = size.height()});
    }
    return programs;
}

bool WineDesktopService::reset(const QString &id)
{
    return m_helper->reset(id);
}
