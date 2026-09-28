// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "paths.h"
#include "session.h"
#include "startup.h"
#include <KProcessList>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <csignal>
#include <unistd.h>
int main(int argc, char **argv)
{
    const QCoreApplication application(argc, argv);
    // Neither installation nor socket inheritance is permission to run elevated.
    if (getuid() == 0 || getuid() != geteuid() || getgid() != getegid()) {
        qCritical() << "The X11 proxy requires an ordinary user session";
        return 1;
    }
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    if (arguments == QStringList{QStringLiteral("--upscale-enabled")}) {
        return UpscaleX11::requestedRouting() ? 0 : 1;
    }
    const QString program = QString::fromLocal8Bit(stockXwayland);
    if (QFileInfo(program).canonicalFilePath() == QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()) {
        qCritical() << "Recursive Xwayland launcher path";
        return 1;
    }
    const KProcessList::KProcessInfo parent = KProcessList::processInfo(getppid());
    const bool kwinParent = parent.isValid() && QFileInfo(parent.name()).fileName() == QLatin1String("kwin_wayland");
    if (!kwinParent || qEnvironmentVariable("UPSCALE_X11_SESSION_ROUTED") != QLatin1String("1")
        || !arguments.contains(QStringLiteral("-listenfd"))) {
        qInfo() << "Upscale X11 routing disabled; executing stock Xwayland directly";
        return UpscaleX11::startDirect(program, arguments);
    }
    std::signal(SIGPIPE, SIG_IGN);
    UpscaleX11::Session session(program);
    if (!session.start(arguments)) {
        return 1;
    }
    return QCoreApplication::exec();
}
