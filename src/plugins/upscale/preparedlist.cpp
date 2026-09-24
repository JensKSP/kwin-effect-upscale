/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "preparedlist.h"

#include <KLocalizedString>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace KWin
{

static QDBusMessage helperCall(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.Upscale.Helper"),
                                          QStringLiteral("/Helper"),
                                          QStringLiteral("org.kde.KWin.Upscale.Helper1"),
                                          method);
}

UpscalePreparedList::UpscalePreparedList(QWidget *parent)
    : QGroupBox(i18nc("@title:group", "Prepared Games"), parent)
    , m_rows(new QVBoxLayout(this))
{
    setObjectName(QStringLiteral("preparedGames"));
    hide();
}

void UpscalePreparedList::refresh()
{
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(helperCall(QStringLiteral("prepared"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        // No helper, or one that failed: nothing to show.
        fill(finished->isError() ? QDBusMessage() : finished->reply());
    });
}

void UpscalePreparedList::fill(const QDBusMessage &reply)
{
    // Each row is a widget of its own, so that it goes with everything in it.
    while (QLayoutItem *item = m_rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QVariantList values = reply.arguments();
    if (values.size() != 1 || !values.first().canConvert<QDBusArgument>()) {
        hide();
        return;
    }
    // a(ssii): identifier, title, width, height.
    const auto programs = values.first().value<QDBusArgument>();
    programs.beginArray();
    while (!programs.atEnd()) {
        QString id;
        QString title;
        int width = 0;
        int height = 0;
        programs.beginStructure();
        programs >> id >> title >> width >> height;
        programs.endStructure();
        auto row = new QWidget(this);
        auto layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel(i18nc("@label a game and the resolution it was prepared for", "%1, rendered at %2 × %3", title,
                                           QString::number(width), QString::number(height)),
                                     row),
                          1);
        auto reset = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), i18nc("@action:button", "Reset"), row);
        reset->setToolTip(i18nc("@info:tooltip", "Undo the preparation. The game renders at full size again from its next start."));
        connect(reset, &QPushButton::clicked, this, [this, id]() {
            this->reset(id);
        });
        layout->addWidget(reset);
        m_rows->addWidget(row);
    }
    programs.endArray();
    setVisible(m_rows->count() > 0);
}

void UpscalePreparedList::reset(const QString &id)
{
    QDBusMessage message = helperCall(QStringLiteral("reset"));
    message.setArguments({id});
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        refresh();
    });
}

} // namespace KWin
