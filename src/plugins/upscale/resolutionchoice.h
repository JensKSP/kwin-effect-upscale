/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

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

}
