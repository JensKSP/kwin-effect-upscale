/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "resolution.h"

#include <QObject>

class QFormLayout;
class QLabel;
class QWidget;

namespace KWin
{

/**
 * What a resolution setting renders at, on each of this system's screens.
 *
 * A preset is a ratio, and "Quality" means little until it is a size on the
 * screen a person plays on. The preview says, for every connected screen,
 * what an application would render at there, or that it would not be
 * upscaled at all. "All applications" and every game show one, each computed
 * from the values that entry would actually use.
 *
 * It lists every screen rather than offering a choice of one: a choice here
 * would look like a setting, and nothing about it is stored.
 */
class UpscaleResolutionPreview : public QObject
{
    Q_OBJECT

public:
    explicit UpscaleResolutionPreview(QObject *parent = nullptr);

    /** Add the preview to @p form, named @p name. */
    void build(QFormLayout *form, QWidget *parent, const QString &name);

    /** Show what these values give; @p basisPoints is Custom's share. */
    void show(ResolutionPreset preset, int basisPoints, int minimumPixels);

private:
    void watchScreens();
    void update();

    QLabel *m_label = nullptr;
    ResolutionPreset m_preset = ResolutionPreset::Native;
    int m_basisPoints = 10000;
    int m_minimumPixels = 0;
};

} // namespace KWin
