/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "helper.h"

#include "compatibility.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

static const QString helperService = QStringLiteral("org.kde.KWin.Upscale.Helper");
static const QString helperPath = QStringLiteral("/Helper");
static const QString helperInterface = QStringLiteral("org.kde.KWin.Upscale.Helper1");

// The screens the program in this window is to see, in the pixels X11 counts
// them in: every output of the session, the window's own first and at the size
// the effect wants it to render at. A helper that describes a whole screen
// needs them all, or the program would see one screen where the session has
// two, which is a change wider than the size it is there for.
static QVariantList screensFor(EffectWindow *window, const QSize &size)
{
    QList<UpscaleProgramScreen> screens;
    UpscaleOutput *own = window->screen();
    if (!own || size.isEmpty()) {
        return {QVariant::fromValue(screens)};
    }
    const qreal scale = kwinApp()->xwaylandScale();
    const auto add = [&screens, scale](UpscaleOutput *output, const QSize &pixels) {
        const QRectF geometry = output->geometryF();
        screens.append({
            .x = qRound(geometry.x() * scale),
            .y = qRound(geometry.y() * scale),
            .width = pixels.width(),
            .height = pixels.height(),
            .rate = upscaleRefreshRate(output),
        });
    };
    add(own, size);
    const QList<UpscaleOutput *> outputs = effects ? effects->screens() : QList<UpscaleOutput *>{};
    for (UpscaleOutput *output : outputs) {
        if (output != own) {
            add(output, (output->geometryF().size() * scale).toSize());
        }
    }
    return {QVariant::fromValue(screens)};
}

// What the helper is told about a window: its process, its class and the
// title a person would recognise it by.
static QVariantList identify(EffectWindow *window)
{
    const Window *internal = window->window();
    return {QVariant::fromValue(uint(window->pid())), internal ? internal->resourceClass() : QString(), window->caption()};
}

void UpscaleHelper::call(const QString &method, const QVariantList &arguments, const std::function<void(const QDBusMessage &)> &reply)
{
    qCDebug(KWIN_UPSCALE) << "Helper call:" << method << arguments;
    QDBusMessage message = QDBusMessage::createMethodCall(helperService, helperPath, helperInterface, method);
    message.setArguments(arguments);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message));
    // The helper owns an unfinished call; completion schedules earlier cleanup.
    watcher->setParent(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method, reply](QDBusPendingCallWatcher *finished) {
        qCDebug(KWIN_UPSCALE) << "Helper reply:" << method << "error" << finished->isError()
                              << finished->reply().errorMessage() << finished->reply().arguments();
        finished->deleteLater();
        // No helper installed, or one that failed: nobody answered.
        if (!finished->isError() && reply) {
            reply(finished->reply());
        }
    });
}

UpscaleHelper::UpscaleHelper()
{
    qDBusRegisterMetaType<UpscaleProgramScreen>();
    qDBusRegisterMetaType<QList<UpscaleProgramScreen>>();
}

void UpscaleHelper::offer(EffectWindow *window, const QSize &size, const Offered &reply)
{
    const QVariantList arguments = identify(window) + screensFor(window, size);
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
    arguments += screensFor(window, wanted);
    call(QStringLiteral("present"), arguments, [reply](const QDBusMessage &message) {
        const QVariantList values = message.arguments();
        if (values.size() == 1) {
            // A size arrives as the two numbers of a D-Bus structure.
            reply(qdbus_cast<QSize>(values.at(0)));
        }
    });
}

} // namespace KWin

QDBusArgument &operator<<(QDBusArgument &argument, const KWin::UpscaleProgramScreen &screen)
{
    argument.beginStructure();
    argument << screen.x << screen.y << screen.width << screen.height << screen.rate;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, KWin::UpscaleProgramScreen &screen)
{
    argument.beginStructure();
    argument >> screen.x >> screen.y >> screen.width >> screen.height >> screen.rate;
    argument.endStructure();
    // The signature QtDBus requires of a demarshaller returns its argument.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return argument;
}
