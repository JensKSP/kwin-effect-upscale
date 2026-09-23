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

QDBusArgument &operator<<(QDBusArgument &argument, const ProgramScreen &screen)
{
    argument.beginStructure();
    argument << screen.x << screen.y << screen.width << screen.height << screen.rate;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, ProgramScreen &screen)
{
    argument.beginStructure();
    argument >> screen.x >> screen.y >> screen.width >> screen.height >> screen.rate;
    argument.endStructure();
    // The signature QtDBus requires of a demarshaller returns its argument.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return argument;
}

// What the effect said, as the helper counts screens.
static QList<WineScreen> screensOf(const QList<ProgramScreen> &screens)
{
    QList<WineScreen> described;
    described.reserve(screens.size());
    for (const ProgramScreen &screen : screens) {
        described.append({.rect = QRect(screen.x, screen.y, screen.width, screen.height), .rate = screen.rate});
    }
    return described;
}

WineDesktopService::WineDesktopService(WineDesktopHelper *helper, QObject *parent)
    : QObject(parent)
    , m_helper(helper)
{
    qDBusRegisterMetaType<PreparedProgram>();
    qDBusRegisterMetaType<QList<PreparedProgram>>();
    qDBusRegisterMetaType<ProgramScreen>();
    qDBusRegisterMetaType<QList<ProgramScreen>>();
}

QString WineDesktopService::offer(uint pid, const QString &windowClass, const QString &title, const QList<ProgramScreen> &screens,
                                 QString &question)
{
    const WineDesktopHelper::Offered offered = m_helper->offer(pid, windowClass, title, screensOf(screens));
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

QSize WineDesktopService::present(uint pid, const QString &windowClass, const QList<ProgramScreen> &wanted)
{
    return m_helper->present(pid, windowClass, screensOf(wanted));
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
