// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QTimer>
namespace UpscaleX11
{
/** Warn when saved settings request removing a transport that cannot migrate live. */
class Lifecycle : public QObject
{
public:
    explicit Lifecycle(QObject *parent);

private:
    void check();
    QTimer m_timer;
    bool m_warned = false;
};
}
