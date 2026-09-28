/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "eligibility.h"
#include "settings.h"

#include "wayland/surface.h"

#include <QHash>
#include <QObject>
#include <QRectF>

#include <utility>

namespace KWin
{

/** Logs changed observations, independently of the on-screen display. */
class UpscaleDiagnostics : public QObject
{
public:
    void observe(EffectWindow *window, const UpscaleSettings &settings, bool selected, UpscaleRefusal refusal);

private:
    // Preserve fractional input regions on newer KWin as well as Qt regions
    // on the supported older releases. These values are observations, not
    // rectangles to round or merge before reporting.
    using InputRegion = decltype(std::declval<SurfaceInterface *>()->input());

    struct State
    {
        QString profile;
        UpscaleMethod method = UpscaleMethod::Off;
        bool enabled = false;
        std::array<int, upscaleSettingCount> settings{};
        QSize buffer;
        QSizeF surface;
        QSizeF destination;
        InputRegion input;
        InputRegion constraint;
        int pointerLock = -1;
        int pointerConfinement = -1;
        QRectF frame;
        QString output;
        QSize outputSize;
        bool selected = false;
        UpscaleRefusal refusal = UpscaleRefusal::None;
        bool operator==(const State &) const = default;
    };
    static State readState(EffectWindow *window, const UpscaleSettings &settings);
    static void logSettings(EffectWindow *window, const QString &id, const State &observed);
    QHash<EffectWindow *, State> m_states;
};

} // namespace KWin
