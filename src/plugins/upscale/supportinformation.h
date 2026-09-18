/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

namespace KWin
{

/**
 * Reads the support information KWin assembles from an effect's properties.
 *
 * Its shape is a line naming the effect, then "<property>: <value>" for each
 * one, the last of which runs over several lines. That framing is KWin's way
 * of reporting, not something a settings page should put in front of a person.
 *
 * Returns the status text, and reports the build the effect named through
 * @p loadedBuild, which stays empty for an effect built without the generated
 * identity record. Deliberately free of KWin types: the settings module is a
 * plain KCModule and does not link the compositor.
 */
QString upscaleReportedStatus(const QString &information, QString *loadedBuild);

} // namespace KWin
