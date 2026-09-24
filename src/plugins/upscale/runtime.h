/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QStringView>

namespace KWin
{

// KWin reports the Unix loader behind a Wine/Proton window, not the Windows
// game's executable. Classifying the runtime does not authorize prefix access:
// the optional helper still proves ownership and obtains the user's consent.
inline bool upscaleWineRuntime(const QString &executable)
{
    const QStringView name = QStringView(executable).mid(executable.lastIndexOf(QLatin1Char('/')) + 1);
    return name == QLatin1String("wine") || name == QLatin1String("wine64")
        || name == QLatin1String("wine-preloader") || name == QLatin1String("wine64-preloader");
}

}
