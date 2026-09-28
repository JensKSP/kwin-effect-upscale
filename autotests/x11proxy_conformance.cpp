// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "paths.h"
#include "session.h"
#include <QCoreApplication>
#include <csignal>
#include <unistd.h>

// XTS needs an unmanaged root. Exercise the production session against a
// rootful server while the live KWin effect still supplies connection policy.
// This test entry point is not installed; the desktop launcher keeps its
// requirement that its parent is KWin.
int main(int argc, char **argv)
{
    const QCoreApplication application(argc, argv);
    if (getuid() == 0 || getuid() != geteuid() || getgid() != getegid()) {
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);
    UpscaleX11::Session session(QString::fromLocal8Bit(stockXwayland));
    if (!session.start(QCoreApplication::arguments().mid(1))) {
        return 1;
    }
    return QCoreApplication::exec();
}
