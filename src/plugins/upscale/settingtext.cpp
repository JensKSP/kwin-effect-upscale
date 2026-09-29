/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The words the settings page and a game's page share for each preference,
// kept apart from settingcontrols.cpp only because that file would otherwise
// pass the size limit.

#include "settingcontrols.h"

#include "resolution.h"

#include <KLocalizedString>

#include <array>

namespace KWin
{

QString upscaleSettingLabel(UpscaleSetting setting)
{
    switch (setting) {
    case UpscaleSetting::Resolution:
        return i18n("Render resolution:");
    case UpscaleSetting::Percentage:
        return i18n("Resolution scale:");
    case UpscaleSetting::MinimumPixels:
        return i18n("Upscale on screens larger than:");
    case UpscaleSetting::Sharpening:
        return i18n("Sharpen the image:");
    case UpscaleSetting::Strength:
        return i18n("Strength:");
    case UpscaleSetting::Geometry:
        return i18n("Picture size:");
    case UpscaleSetting::Filter:
        return i18n("Scaling filter:");
    case UpscaleSetting::Osd:
        return i18n("On-screen display:");
    case UpscaleSetting::OsdDetection:
        return i18n("Show information at startup:");
    case UpscaleSetting::OsdSummary:
        return i18n("Include details:");
    case UpscaleSetting::OsdStatistics:
        return i18n("Show frame rate:");
    case UpscaleSetting::OsdDeveloper:
        return i18n("Show developer information:");
    case UpscaleSetting::OsdTimeout:
        return i18n("Show startup information for:");
    case UpscaleSetting::AnnouncementPosition:
        return i18n("Startup information position:");
    case UpscaleSetting::StatisticsPosition:
        return i18n("Frame rate position:");
    case UpscaleSetting::DeveloperPosition:
        return i18n("Developer information position:");
    }
    return QString();
}

int upscaleSettingChoiceCount(UpscaleSetting setting)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    return info.type == UpscaleSettingType::Choice ? info.maximum - info.minimum + 1 : 0;
}

QString upscaleSettingChoiceLabel(UpscaleSetting setting, int value)
{
    if (setting == UpscaleSetting::Resolution) {
        // Indexed by the stored number rather than cast to a preset, as the
        // corners below are, so that a number outside the list names none.
        static constexpr std::array<ResolutionPreset, 6> s_presets{
            ResolutionPreset::Native,
            ResolutionPreset::UltraQuality,
            ResolutionPreset::Quality,
            ResolutionPreset::Balanced,
            ResolutionPreset::Performance,
            ResolutionPreset::Custom,
        };
        if (value < 0 || value >= int(s_presets.size())) {
            return QString();
        }
        switch (s_presets[std::size_t(value)]) {
        case ResolutionPreset::Native:
            return i18n("Native");
        case ResolutionPreset::UltraQuality:
            return i18n("Ultra Quality");
        case ResolutionPreset::Quality:
            return i18n("Quality");
        case ResolutionPreset::Balanced:
            return i18n("Balanced");
        case ResolutionPreset::Performance:
            return i18n("Performance");
        case ResolutionPreset::Custom:
            return i18n("Custom");
        }
        return QString();
    }
    if (setting == UpscaleSetting::Geometry) {
        switch (value) {
        case int(UpscaleGeometry::Fit):
            return i18nc("Picture size", "Fit to the screen");
        case int(UpscaleGeometry::Integer):
            return i18nc("Picture size", "Whole-number multiple");
        default:
            return QString();
        }
    }
    if (setting == UpscaleSetting::Filter) {
        switch (value) {
        case int(UpscaleFilter::Fsr):
            return i18nc("Scaling filter", "FSR 1");
        case int(UpscaleFilter::Nearest):
            return i18nc("Scaling filter", "Nearest neighbor");
        default:
            return QString();
        }
    }
    // The three position preferences share one list, in the order stored. The
    // stored number indexes it rather than being cast to a corner, so that a
    // number outside it names no corner instead of one that does not exist.
    static constexpr std::array<UpscaleCorner, 4> s_corners{
        UpscaleCorner::TopLeft,
        UpscaleCorner::TopRight,
        UpscaleCorner::BottomLeft,
        UpscaleCorner::BottomRight,
    };
    if (value < 0 || value >= int(s_corners.size())) {
        return QString();
    }
    switch (s_corners[std::size_t(value)]) {
    case UpscaleCorner::TopLeft:
        return i18n("Top left");
    case UpscaleCorner::TopRight:
        return i18n("Top right");
    case UpscaleCorner::BottomLeft:
        return i18n("Bottom left");
    case UpscaleCorner::BottomRight:
        return i18n("Bottom right");
    }
    return QString();
}

QString upscaleSettingToolTip(UpscaleSetting setting)
{
    switch (setting) {
    case UpscaleSetting::Geometry:
        return i18n("“Fit to the screen” enlarges the picture as far as the filter allows without stretching it. "
                    "“Whole-number multiple” enlarges it by the largest whole factor that fits, for pixel art and older "
                    "games. Both leave black bars where the picture does not fill the screen.");
    case UpscaleSetting::Filter:
        return i18n("FSR 1 enlarges at most two times, as a whole-number multiple exactly two times, and can sharpen. "
                    "Nearest neighbor repeats each pixel exactly and enlarges by any amount.");
    default:
        return QString();
    }
}

} // namespace KWin
