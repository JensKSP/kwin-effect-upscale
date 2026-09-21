/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "legacysettings.h"

#include "application.h"
#include "resolution.h"

#include <KConfigGroup>

#include <algorithm>

namespace KWin
{

std::optional<int> upscaleLegacyResolution(const KConfigGroup &global)
{
    if (global.hasKey("Resolution") || !global.hasKey("Preset")) {
        return std::nullopt;
    }
    const int stored = global.readEntry("Preset", 0);
    // Zero was Automatic, and "nobody chose" is now said by storing nothing.
    if (stored <= 0) {
        return std::nullopt;
    }
    return std::clamp(stored - 1, 0, int(ResolutionPreset::Custom));
}

bool upscaleLegacyUnlisted(const KConfigGroup &global)
{
    return !global.hasKey("UnlistedApplications") && global.readEntry("UnknownApplications", false);
}

bool upscaleLegacySwitchedOff(const KConfigGroup &global)
{
    // Only a stored false counts. The old default was on, so an absent key
    // said nothing, and a stored true said nothing the new model does not
    // already say.
    return global.hasKey("Enabled") && !global.readEntry("Enabled", true);
}

void upscaleForgetLegacySettings(KConfigGroup &global)
{
    global.deleteEntry("Preset");
    global.deleteEntry("UnknownApplications");
}

void upscaleReadLegacyOverrides(const KConfigGroup &profile, UpscaleSettingOverrides &overrides)
{
    std::optional<int> &resolution = overrides[std::size_t(UpscaleSetting::Resolution)];
    if (!profile.hasKey("Resolution") && profile.hasKey("Preset")) {
        const QString stored = profile.readEntry("Preset", QString());
        if (stored != QLatin1String("Automatic")) {
            resolution = int(upscalePresetFromKey(stored, ResolutionPreset::Native));
        }
    }
    std::optional<int> &threshold = overrides[std::size_t(UpscaleSetting::MinimumPixels)];
    if (threshold && *threshold < 0) {
        threshold.reset();
    }
}

void upscaleRetireLegacyProfileKeys(KConfigGroup &profile, const UpscaleApplication &application)
{
    // A shipped file never carries these any more, so a key with a default
    // behind it belongs to a package that is not this one; leave it to that.
    if (profile.hasKey("Method") && !profile.hasDefault("Method")) {
        for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
            if (application.methods[slot] != UpscaleMethod::Auto) {
                profile.writeEntry(upscalePresentationKey(UpscalePresentation(slot)),
                                   upscaleMethodKey(application.methods[slot]));
            }
        }
        profile.deleteEntry("Method");
    }
    if (profile.hasKey("Preset") && !profile.hasDefault("Preset")) {
        if (const std::optional<int> &resolution = application.overrides[std::size_t(UpscaleSetting::Resolution)]) {
            profile.writeEntry("Resolution", upscalePresetKey(ResolutionPreset(*resolution)));
        }
        profile.deleteEntry("Preset");
    }
}

} // namespace KWin
