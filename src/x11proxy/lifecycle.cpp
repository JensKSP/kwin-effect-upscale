// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lifecycle.h"
#include "startup.h"
#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>
#include <QVariantMap>
namespace UpscaleX11
{
Lifecycle::Lifecycle(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &Lifecycle::check);
    m_timer.start();
}
void Lifecycle::check()
{
    if (requestedRouting()) {
        m_warned = false;
        return;
    }
    if (m_warned) {
        return;
    }
    m_warned = true;
    qInfo() << "Upscale X11 restart required: saved settings disable routing; existing connections remain until logout";
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                          QStringLiteral("/org/freedesktop/Notifications"), QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    message << i18n("Upscale") << uint(0) << QStringLiteral("preferences-system-windows")
            << i18n("Restart required")
            << i18n("Log out and back in to remove the X11 proxy. Existing X11 connections still use it until logout.")
            << QStringList{} << QVariantMap{} << -1;
    QDBusConnection::sessionBus().asyncCall(message);
}
}
