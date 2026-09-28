/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "legacysettings.h"

#include "application.h"
#include "resolution.h"

#include <KConfigGroup>

#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <utility>

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

void upscaleReadLegacyProgram(const KConfigGroup &profile, UpscaleApplication &application)
{
    if (profile.hasKey("Executable")) {
        return;
    }
    const QString program = profile.readEntry("Program", QString());
    if (program.isEmpty()) {
        return;
    }
    const bool advertises = upscaleIsAdvertisement(
        application.methods[std::size_t(upscaleAdvertisedPresentation())].value_or(UpscaleMethod::Auto));
    const bool statesWindow = !application.windowClass.isEmpty() || !application.instance.isEmpty();
    if (statesWindow && !advertises) {
        return;
    }
    application.executable = QStringLiteral(".*/") + QRegularExpression::escape(program);
    application.executableMatch = UpscaleStringMatch::RegularExpression;
    application.windowClass.clear();
    application.instance.clear();
}

// The Program half of upscaleRetireLegacyProfileKeys(): its value under the
// current keys, and the window identity its reading left out removed as well.
static void retireLegacyProgram(KConfigGroup &profile, const UpscaleApplication &application)
{
    if (!profile.hasKey("Program") || profile.hasDefault("Program")) {
        return;
    }
    if (!application.executable.isEmpty() && !profile.hasKey("Executable")) {
        profile.writeEntry("Executable", application.executable);
        profile.writeEntry("ExecutableMatch", upscaleStringMatchKey(application.executableMatch));
    }
    // A window identity the reading above left out goes as well, or the next
    // reading would require it beside the path and the entry would stop
    // advertising. One the package supplies is hidden rather than deleted,
    // because deleting would let the package's value show through.
    const std::array<std::pair<const char *, bool>, 2> identity{{
        {"WindowClass", !application.windowClass.isEmpty()},
        {"Instance", !application.instance.isEmpty()},
    }};
    for (const auto &[key, stated] : identity) {
        if (stated || !profile.hasKey(key)) {
            continue;
        }
        if (profile.hasDefault(key)) {
            profile.writeEntry(key, QString());
        } else {
            profile.deleteEntry(key);
        }
    }
    profile.deleteEntry("Program");
}

void upscaleRetireLegacyProfileKeys(KConfigGroup &profile, const UpscaleApplication &application)
{
    // A shipped file never carries these any more, so a key with a default
    // behind it belongs to a package that is not this one; leave it to that.
    if (profile.hasKey("Method") && !profile.hasDefault("Method")) {
        for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
            if (const std::optional<UpscaleMethod> method = application.methods[slot]) {
                profile.writeEntry(upscalePresentationKey(UpscalePresentation(slot)), upscaleMethodKey(*method));
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
    retireLegacyProgram(profile, application);
}

} // namespace KWin
