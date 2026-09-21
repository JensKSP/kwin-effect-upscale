/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Add from Window: KWin picks the window, the effect names its program, and
// the editor turns the two answers into an entry. Apart from the rest of the
// editor because it is a conversation over the bus rather than editing.

#include "applicationeditor.h"

#include "identitycontrols.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>

namespace KWin
{

void UpscaleApplicationEditor::addFromWindow()
{
    if (m_selecting || m_programQuery) {
        return;
    }
    m_selecting = true;
    // Keep the settings event loop responsive during KWin's interactive picker.
    // The watcher is owned by this editor; closing it cancels our reply handler.
    // KWin performs the selection itself, for native and X11 windows alike.
    const QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                                QStringLiteral("org.kde.KWin"), QStringLiteral("queryWindowInfo"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        m_selecting = false;
        if (!reply.isValid()) {
            // Cancelling the selection is an error reply, and not a failure worth
            // a dialog. Anything else is worth saying out loud.
            if (reply.error().name() != QLatin1String("org.kde.KWin.Error.UserCancel")) {
                QMessageBox::warning(this, i18n("Add from Window"),
                                     i18n("The window could not be identified: %1", reply.error().message()));
            }
            return;
        }
        m_picked = reply.value();
        askForProgram();
    });
}

void UpscaleApplicationEditor::askForProgram()
{
    // KWin's answer names the window but, in 6.3, not its process. The effect
    // can ask KWin for that, so the page asks the effect; without the effect
    // loaded nobody answers, and the window's identity is all there is to go
    // on.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                          QStringLiteral("org.kde.KWin.Effect.Upscale1"),
                                                          QStringLiteral("executablePath"));
    message << m_picked.value(QStringLiteral("uuid")).toString();
    m_programQuery = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(m_programQuery.data(), &QDBusPendingCallWatcher::finished, this, [this]() {
        const QDBusPendingReply<QString> executable = *m_programQuery;
        m_programQuery->deleteLater();
        addIdentified(m_picked, executable.isValid() ? executable.value() : QString());
    });
}

void UpscaleApplicationEditor::addIdentified(const QVariantMap &information, const QString &executable)
{
    const QString windowClass = information.value(QStringLiteral("resourceClass")).toString();
    const QString instance = information.value(QStringLiteral("resourceName")).toString();
    UpscaleApplication application;
    // The exact path names this copy of this program, and it is what a
    // Wayland game is found by before its window exists. A runtime many games
    // share names none of them, and neither does a path that did not resolve;
    // then the window's identity is what names the game.
    if (upscaleIdentifiesOneProgram(executable)) {
        application.executable = executable;
    } else {
        application.windowClass = windowClass;
        application.instance = instance;
    }
    if (application.executable.isEmpty() && application.windowClass.isEmpty() && application.instance.isEmpty()) {
        QMessageBox::warning(this, i18n("Add from Window"), i18n("The window does not identify its application."));
        return;
    }
    application.name = windowClass;
    if (application.name.isEmpty()) {
        application.name = instance.isEmpty() ? executable.section(QLatin1Char('/'), -1) : instance;
    }
    application.id = upscaleNewApplicationId(application.name, m_applications);
    application.order = m_applications.empty() ? 100 : m_applications.back().order + 10;
    m_applications.push_back(application);
    m_original.push_back(UpscaleApplication{});
    rebuildList();
    m_list->setCurrentRow(rowOf(m_applications.size() - 1));
    m_name->setFocus();
    Q_EMIT changed();
}

} // namespace KWin
