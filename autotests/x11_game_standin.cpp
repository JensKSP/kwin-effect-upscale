/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A game's X11 window in a process of its own, which a test starts under a
// Wine loader's name. KWin 6.6 takes an X11 window's process from the
// connection that made it, through XRes, where 6.3 took it from _NET_WM_PID,
// so a window the test process makes can no longer claim to be Wine's. This
// one is made by the process whose name says so, and names itself in
// _NET_WM_PID for 6.3 as well.
//
// Arguments: the window's class, its width and height, when it asks for
// fullscreen - as it maps (on-map), right after (after-map) or not at all
// (never) - and optionally "anonymous", which leaves _NET_WM_PID unset, as a
// few programs do. It reports each change a test asks about on standard
// output, one per line - "fullscreen 0|1", "geometry X Y W H", "configured W H",
// "mapped W H" and "close" - and exits when its standard input closes.

#include "x11_client.h"

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTimer>

#include <cstdio>

#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() != 5 && !(arguments.size() == 6 && arguments.at(5) == QLatin1String("anonymous"))) {
        std::fprintf(stderr, "usage: %s CLASS WIDTH HEIGHT on-map|after-map|never [anonymous]\n", argv[0]);
        return 2;
    }
    const QString when = arguments.at(4);
    X11Client game(false);
    if (arguments.size() == 5) {
        game.reportProcess();
    }
    if (!game.show(arguments.at(1).toLatin1(), QRect(0, 0, arguments.at(2).toInt(), arguments.at(3).toInt()),
                   when == QLatin1String("on-map"))) {
        return 3;
    }
    if (when == QLatin1String("after-map")) {
        game.fullscreen(true);
    }

    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, &application, []() {
        char buffer[64];
        if (read(STDIN_FILENO, buffer, sizeof(buffer)) <= 0) {
            QCoreApplication::quit();
        }
    });

    // The client keeps what it received rather than announcing it, so its
    // state is compared with what was last reported and every change printed.
    bool fullscreen = false;
    QRect geometry;
    qsizetype configured = 0;
    int closes = 0;
    bool mapped = false;
    QTimer report;
    QObject::connect(&report, &QTimer::timeout, &application, [&]() {
        if (game.isFullscreen() != fullscreen) {
            fullscreen = game.isFullscreen();
            std::printf("fullscreen %d\n", int(fullscreen));
        }
        if (game.geometry() != geometry) {
            geometry = game.geometry();
            std::printf("geometry %d %d %d %d\n", geometry.x(), geometry.y(), geometry.width(), geometry.height());
        }
        const QList<QSize> sizes = game.configuredSizes();
        for (; configured < sizes.size(); ++configured) {
            std::printf("configured %d %d\n", sizes.at(configured).width(), sizes.at(configured).height());
        }
        if (!mapped && game.sizeAtMapping().isValid()) {
            mapped = true;
            std::printf("mapped %d %d\n", game.sizeAtMapping().width(), game.sizeAtMapping().height());
        }
        for (; closes < game.closeRequests(); ++closes) {
            std::printf("close\n");
        }
        std::fflush(stdout);
    });
    report.start(10);
    return application.exec();
}
