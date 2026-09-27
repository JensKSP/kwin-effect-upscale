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
