/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "helper.h"

#include "compatibility.h"

#include "effect/effectwindow.h"
#include "window.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>

namespace KWin
{

static const QString helperService = QStringLiteral("org.kde.KWin.Upscale.Helper");
static const QString helperPath = QStringLiteral("/Helper");
static const QString helperInterface = QStringLiteral("org.kde.KWin.Upscale.Helper1");

// What the helper is told about a window: its process, its class and the
// title a person would recognise it by.
static QVariantList identify(EffectWindow *window)
{
    const Window *internal = window->window();
    return {QVariant::fromValue(uint(window->pid())), internal ? internal->resourceClass() : QString(), window->caption()};
}

void UpscaleHelper::call(const QString &method, const QVariantList &arguments, const std::function<void(const QDBusMessage &)> &reply)
{
    QDBusMessage message = QDBusMessage::createMethodCall(helperService, helperPath, helperInterface, method);
    message.setArguments(arguments);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [reply](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        // No helper installed, or one that failed: nobody answered.
        if (!finished->isError() && reply) {
            reply(finished->reply());
        }
    });
}

void UpscaleHelper::offer(EffectWindow *window, const QSize &size, const Offered &reply)
{
    const QVariantList arguments = identify(window) + QVariantList{QVariant::fromValue(size), upscaleRefreshRate(window->screen())};
    call(QStringLiteral("offer"), arguments, [reply](const QDBusMessage &message) {
        const QVariantList values = message.arguments();
        if (values.size() == 2) {
            reply(values.at(0).toString(), values.at(1).toString());
        }
    });
}

void UpscaleHelper::answer(const QString &offer, const QString &answer, const Answered &reply)
{
    call(QStringLiteral("answer"), {offer, answer}, [reply](const QDBusMessage &message) {
        const QVariantList values = message.arguments();
        if (values.size() == 1) {
            reply(values.at(0).toString());
        }
    });
}

void UpscaleHelper::restart(const QString &offer)
{
    call(QStringLiteral("restart"), {offer}, nullptr);
}

void UpscaleHelper::present(EffectWindow *window, const QSize &wanted, const Prepared &reply)
{
    QVariantList arguments = identify(window);
    arguments.removeLast();
    arguments << QVariant::fromValue(wanted) << upscaleRefreshRate(window->screen());
    call(QStringLiteral("present"), arguments, [reply](const QDBusMessage &message) {
        const QVariantList values = message.arguments();
        if (values.size() == 1) {
            // A size arrives as the two numbers of a D-Bus structure.
            reply(qdbus_cast<QSize>(values.at(0)));
        }
    });
}

} // namespace KWin
