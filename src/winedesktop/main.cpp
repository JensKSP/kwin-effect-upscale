/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedesktophelper.h"
#include "winedesktopservice.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

/*
 * The session service org.kde.KWin.Upscale.Helper, started by D-Bus when the
 * effect first asks. It leaves again once nothing waits for it.
 */
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kwin-upscale-helper"));
    KLocalizedString::setApplicationDomain(TRANSLATION_DOMAIN);

    const QString state = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    QDir().mkpath(state);
    WineDesktopRecords records(state + QStringLiteral("/winedesktop"));
    WineDesktopHelper helper(&records);
    WineDesktopService service(&helper);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/Helper"), &service, QDBusConnection::ExportScriptableSlots) || !bus.registerService(QStringLiteral("org.kde.KWin.Upscale.Helper"))) {
        return 1;
    }

    // Leaves after a minute with nothing to wait for; the next question from
    // the effect starts it again.
    QTimer idle;
    idle.setInterval(std::chrono::minutes(1));
    QObject::connect(&idle, &QTimer::timeout, &application, [&helper] {
        if (!helper.busy()) {
            QCoreApplication::quit();
        }
    });
    idle.start();
    return QCoreApplication::exec();
}
