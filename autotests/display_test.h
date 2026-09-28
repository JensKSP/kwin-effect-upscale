/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "display.h"
#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"

#include <QTest>

using namespace KWin;

class UpscaleDisplayTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void visibilityAndSampling();
    void blocksKeepTheirOwnCorners();
    void displayShowsTheState();
    void perGameDisplaySettings();

private:
    QImage paint(UpscaleDisplay &display);
    QImage paintOn(UpscaleDisplay &display, const QSize &size, const UpscaleRectF &screen);
    std::unique_ptr<EglDisplay> m_display;
    std::shared_ptr<EglContext> m_context;
};
