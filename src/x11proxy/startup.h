// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QList>
#include <QStringList>
namespace UpscaleX11
{
bool requestedRouting();
int startDirect(const QString &program, const QStringList &arguments);
bool socketFlags(int descriptor, bool nonblocking = true);
quint32 peerProcess(int descriptor);
// Reports that starting the session failed at @p step, for @p reason, and
// returns false. Each step reports its own failure where it happens: errno read
// once the whole start has failed belongs to whatever call ran last, which was
// how a socket path too long for its address came to be reported as "File
// exists", left behind by QTemporaryDir's search for an unused name.
bool startupFailed(const QString &step, const QString &reason);
// The system's text for @p error, taken while it still describes the call that
// failed.
QString systemError(int error);
struct Startup
{
    QStringList arguments;
    QList<int> listeners;
    QList<int> childDescriptors;
    int windowManager = -1;
    int windowManagerBackend = -1;
    bool parse(const QStringList &original, int backendListener);

private:
    bool descriptorArgument(const QString &argument, int descriptor, int backendListener);
};
}
