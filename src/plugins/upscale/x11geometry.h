/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "config-kwin.h"

#if KWIN_BUILD_X11
#include <QPoint>
#include <QSize>

namespace KWin
{
class X11Window;

QPoint upscaleX11Position(const X11Window *window);
QSize upscaleX11NormalSize(const X11Window *window);
bool upscaleX11ModeAvailable(const QPoint &position, const QSize &size);
bool upscaleX11PrimaryOutput(const QPoint &position);
bool upscaleX11ModeMatches(X11Window *window, const QPoint &position, const QSize &size);
void upscaleX11Configure(X11Window *window, const QPoint &position, const QSize &size, bool notify = false);
}
#endif
