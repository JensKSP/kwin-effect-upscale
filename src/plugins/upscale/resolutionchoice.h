/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QSize>
#include <QString>

class QComboBox;

namespace KWin
{

/**
 * The threshold control, which deals in resolutions and stores pixels.
 *
 * The effect compares a number of pixels, because that is the comparison an
 * output threshold needs: it has to answer "is this screen bigger than that"
 * for screens of any shape. Nobody has an opinion about 2073600, though, and
 * a person setting a threshold is thinking of a screen they own. So the
 * control offers resolutions - the ones their screens are actually running,
 * and the common ones besides - and converts.
 */

/** Fill @p box with the resolutions this system is showing, and the usual ones. */
void upscaleFillResolutions(QComboBox *box);

/** Select @p pixels in @p box, as a resolution where one matches that count. */
void upscaleSelectResolution(QComboBox *box, int pixels);

/**
 * The pixel count @p box currently describes, or @p fallback where its text is
 * not a resolution at all. Accepts what people type: 1920x1080, 1920 × 1080,
 * spaces or none.
 */
int upscaleResolutionPixels(const QComboBox *box, int fallback);

/**
 * @p pixels as a person would name it: a resolution where one of the offered
 * ones has that count, "Any screen" for none at all, and the count otherwise.
 */
QString upscaleResolutionName(int pixels);

/** A connected screen as the settings page names it: its physical size and name. */
struct UpscaleScreen
{
    QSize pixels;
    QString name;
};

/** The connected screens, in physical pixels, in the order Qt lists them. */
QList<UpscaleScreen> upscaleScreens();

/** The screen with the most pixels, the one a game is most likely upscaled on. */
UpscaleScreen upscaleLargestScreen();

/**
 * The scales, in basis points, at which @p screen renders a resolution people
 * know - 2560 × 1440 on a 3840 × 2160 screen is 6667 - from half the screen
 * to all of it, ascending.
 *
 * Only resolutions of the screen's own shape qualify, and only where the
 * scale reproduces them to the pixel, so that a scale snapped to one renders
 * exactly that. Stored as a scale, it gives the same share on any other
 * screen, which is what keeps a setting portable.
 */
QList<int> upscaleSnapScales(const QSize &screen);

}
