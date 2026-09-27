// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "relay.h"
#include <QCoreApplication>
#include <QStringList>
#include <csignal>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    std::signal(SIGPIPE, SIG_IGN);
    if (argc != 3 && argc != 4) {
        return 2;
    }
    // A third argument, the advertised size as WIDTHxHEIGHT, puts the protocol
    // policy in front as a selected connection has it; without it the relay
    // forwards bytes unread.
    UpscaleX11::ConnectionPolicy policy;
    if (argc == 4) {
        const QStringList size = application.arguments()[3].split(QLatin1Char('x'));
        policy.size = QSize(size.value(0).toInt(), size.value(1).toInt());
    }
    auto *relay = new UpscaleX11::Relay(application.arguments()[1].toInt(), application.arguments()[2].toInt(), &application, policy);
    QObject::connect(relay, &QObject::destroyed, &application, &QCoreApplication::quit);
    return application.exec();
}
