/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settings.h"

#include "application.h"
#include "legacysettings.h"
#include "upscaleconfig.h"

#include <KConfigGroup>

#include <algorithm>
#include <limits>

namespace KWin
{

// The global group, for the few readers that have to ask whether a key is
// stored at all rather than what the generated accessor would return.
static KConfigGroup globalGroup()
{
    return KConfigGroup(UpscaleConfig::self()->config(), QStringLiteral("Effect-upscale"));
}

// The accessors themselves. One each rather than a switch, because a row then
// says everything about its preference on one line, and adding a preference
// never touches a function that already has fifteen branches in it.
// Honouring what a previous release stored under its old name, for as long as
// no new key has replaced it.
static int globalResolution()
{
    return upscaleLegacyResolution(globalGroup()).value_or(UpscaleConfig::resolution());
}

// The scale is held in basis points, hundredths of a percent, which is what
// resolutionRatio() takes; see there why a whole percent is not enough.
static int globalPercentage()
{
    return qRound(UpscaleConfig::percentage() * 100);
}

// A file spells the scale as the percentage it is, with the decimals it needs
// and no more: "66.67", "75". A whole percentage written by an earlier version
// therefore reads as the same share it always meant.
static QString percentageName(int basisPoints)
{
    QString text = QString::number(basisPoints / 100);
    if (const int hundredths = basisPoints % 100) {
        text += QStringLiteral(".%1").arg(hundredths, 2, 10, QLatin1Char('0'));
        if (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
    }
    return text;
}

static int percentageValue(const QString &name, int absent)
{
    // QString::toDouble() reads the C locale's decimal point whatever the
    // user's locale is, which is what a configuration file is written in.
    bool valid = false;
    const double percentage = name.trimmed().toDouble(&valid);
    return valid ? qRound(percentage * 100) : absent;
}

static int globalMinimumPixels()
{
    return UpscaleConfig::minimumPixels();
}

static int globalSharpening()
{
    return int(UpscaleConfig::sharpening());
}

static int globalStrength()
{
    return UpscaleConfig::strength();
}

static int globalOsd()
{
    return int(UpscaleConfig::osd());
}

static int globalOsdDetection()
{
    return int(UpscaleConfig::osdDetection());
}

static int globalOsdSummary()
{
    return int(UpscaleConfig::osdSummary());
}

static int globalOsdStatistics()
{
    return int(UpscaleConfig::osdStatistics());
}

static int globalOsdDeveloper()
{
    return int(UpscaleConfig::osdDeveloper());
}

static int globalOsdTimeout()
{
    return UpscaleConfig::osdTimeout();
}

static int globalAnnouncementPosition()
{
    return UpscaleConfig::osdAnnouncementPosition();
}

static int globalStatisticsPosition()
{
    return UpscaleConfig::osdStatisticsPosition();
}

static int globalDeveloperPosition()
{
    return UpscaleConfig::osdDeveloperPosition();
}

// The resolution is spelled by name in a configuration file, where a number
// would be a puzzle whose answer changes if the enumeration gains a value.
static QString resolutionName(int value)
{
    return upscalePresetKey(ResolutionPreset(value));
}

static int resolutionValue(const QString &name, int absent)
{
    return int(upscalePresetFromKey(name, ResolutionPreset(absent)));
}

// One row per preference, in enumerator order, each naming the accessor that
// yields the global layer's current value. The defaults those accessors return
// are declared in upscaleconfig.kcfg and nowhere else.
//
// Two keys are deliberately not what the previous release used:
//
//   Resolution, not Preset. The preset enumeration loses Automatic, so every
//   value after it shifts by one. A renamed key cannot be misread by either
//   release, where the same key would have quietly turned a stored Quality
//   into Balanced on the first run of this one.
//
//   The unlisted-application setting is gone entirely. It was never a
//   preference: it said what to do with a window no profile claimed, which is
//   the global profile's own method, and methods are not in this table.
//
// A constant rather than a function body: every member is a literal or a
// pointer to a function above, so the whole table is fixed at compile time and
// no reader can observe it half built.
static const std::array<UpscaleSettingInfo, upscaleSettingCount> settingTable{{
    {UpscaleSetting::Resolution, "Resolution", UpscaleSettingType::Choice, 0, int(ResolutionPreset::Custom), globalResolution, resolutionName, resolutionValue},
    {UpscaleSetting::Percentage, "Percentage", UpscaleSettingType::Number, 5000, 10000, globalPercentage, percentageName, percentageValue},
    {UpscaleSetting::MinimumPixels, "MinimumPixels", UpscaleSettingType::Number, 0, std::numeric_limits<int>::max(), globalMinimumPixels, nullptr, nullptr},
    {UpscaleSetting::Sharpening, "Sharpening", UpscaleSettingType::Switch, 0, 1, globalSharpening, nullptr, nullptr},
    {UpscaleSetting::Strength, "Strength", UpscaleSettingType::Number, 0, 100, globalStrength, nullptr, nullptr},
    {UpscaleSetting::Osd, "Osd", UpscaleSettingType::Switch, 0, 1, globalOsd, nullptr, nullptr},
    {UpscaleSetting::OsdDetection, "OsdDetection", UpscaleSettingType::Switch, 0, 1, globalOsdDetection, nullptr, nullptr},
    {UpscaleSetting::OsdSummary, "OsdSummary", UpscaleSettingType::Switch, 0, 1, globalOsdSummary, nullptr, nullptr},
    {UpscaleSetting::OsdStatistics, "OsdStatistics", UpscaleSettingType::Switch, 0, 1, globalOsdStatistics, nullptr, nullptr},
    {UpscaleSetting::OsdDeveloper, "OsdDeveloper", UpscaleSettingType::Switch, 0, 1, globalOsdDeveloper, nullptr, nullptr},
    {UpscaleSetting::OsdTimeout, "OsdTimeout", UpscaleSettingType::Number, 1, 60, globalOsdTimeout, nullptr, nullptr},
    {UpscaleSetting::AnnouncementPosition, "OsdAnnouncementPosition", UpscaleSettingType::Choice, 0, upscaleCornerCount - 1, globalAnnouncementPosition, nullptr, nullptr},
    {UpscaleSetting::StatisticsPosition, "OsdStatisticsPosition", UpscaleSettingType::Choice, 0, upscaleCornerCount - 1, globalStatisticsPosition, nullptr, nullptr},
    {UpscaleSetting::DeveloperPosition, "OsdDeveloperPosition", UpscaleSettingType::Choice, 0, upscaleCornerCount - 1, globalDeveloperPosition, nullptr, nullptr},
}};

const std::array<UpscaleSettingInfo, upscaleSettingCount> &upscaleSettingTable()
{
    return settingTable;
}

const UpscaleSettingInfo &upscaleSettingInfo(UpscaleSetting setting)
{
    // The table is in enumerator order. An autotest asserts that rather than
    // this comment promising it, because a row inserted in the wrong place
    // would silently give every later preference the wrong key.
    return upscaleSettingTable()[std::size_t(setting)];
}

UpscaleSettingOverrides upscaleReadOverrides(const KConfigGroup &group)
{
    UpscaleSettingOverrides overrides;
    for (const UpscaleSettingInfo &info : upscaleSettingTable()) {
        // Presence is the whole question. A profile that says nothing about a
        // preference inherits it, and false and zero are answers rather than
        // absences, so this asks the group rather than comparing values.
        if (!group.hasKey(info.key)) {
            continue;
        }
        overrides[std::size_t(info.setting)] = info.value
            ? info.value(group.readEntry(info.key, QString()), info.global())
            : group.readEntry(info.key, info.global());
    }
    upscaleReadLegacyOverrides(group, overrides);
    return overrides;
}

void upscaleWriteOverrides(KConfigGroup &group, const UpscaleSettingOverrides &overrides,
                           const UpscaleSettingOverrides &original)
{
    for (const UpscaleSettingInfo &info : upscaleSettingTable()) {
        const auto entry = std::size_t(info.setting);
        const std::optional<int> &stated = overrides[entry];
        if (stated == original[entry]) {
            continue;
        }
        if (stated) {
            if (info.name) {
                group.writeEntry(info.key, info.name(*stated));
            } else {
                group.writeEntry(info.key, *stated);
            }
        } else if (group.hasDefault(info.key)) {
            // The installed package states this one. Reverting restores what
            // it says; deleting would write the marker that hides it, and
            // keep hiding every later package's value as well.
            group.revertToDefault(info.key);
        } else {
            group.deleteEntry(info.key);
        }
    }
}

bool UpscaleSettings::acts() const
{
    return m_acts;
}

void UpscaleSettings::setActs(bool acts)
{
    m_acts = acts;
}

int UpscaleSettings::value(UpscaleSetting setting) const
{
    return m_values[std::size_t(setting)];
}

void UpscaleSettings::setValue(UpscaleSetting setting, int value)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    // A configuration file can hold anything at all. Clamping once, here, is
    // what lets every accessor below cast without checking and every reader
    // use the value as it stands. The alternative is each reader defending
    // itself, which is how the -1 that used to mean "inherit" ended up inside
    // a pixel count's own value space.
    m_values[std::size_t(setting)] = std::clamp(value, info.minimum, info.maximum);
}

bool UpscaleSettings::switchedOn(UpscaleSetting setting) const
{
    return value(setting) != 0;
}

UpscaleCorner UpscaleSettings::corner(UpscaleSetting setting) const
{
    // No clamping here: setValue() already held every entry inside its row's
    // bounds, which for a corner is exactly the four that exist. Asking
    // upscaleCorner() to clamp it again would only add a dependency on the
    // placement code to every target that reads a setting.
    return UpscaleCorner(value(setting));
}

ResolutionPreset UpscaleSettings::resolution() const
{
    return ResolutionPreset(value(UpscaleSetting::Resolution));
}

double UpscaleSettings::sharpening() const
{
    return sharpeningAmount(switchedOn(UpscaleSetting::Sharpening), value(UpscaleSetting::Strength));
}

UpscaleMethods upscaleGlobalMethods()
{
    UpscaleMethods methods;
    const KConfigGroup group = globalGroup();
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const auto presentation = UpscalePresentation(slot);
        // Off where nothing states one: an unmeasured program is asked for
        // nothing until somebody says otherwise. That is the opposite of a
        // profile, whose silence means Auto, and it is deliberate - a profile
        // describes a game somebody looked at.
        methods[slot] = upscaleMethodFromKey(group.readEntry(upscalePresentationKey(presentation), QString()),
                                             UpscaleMethod::Off);
        if (!upscaleMethodApplies(presentation, methods[slot])) {
            methods[slot] = UpscaleMethod::Off;
        }
    }
    // The old unlisted-application setting said something to such programs,
    // and what it said was the advertised mode at bind. That is the fullscreen
    // slot's answer, because that is the slot read before a window exists.
    const auto advertised = std::size_t(upscaleAdvertisedPresentation());
    if (!group.hasKey(upscalePresentationKey(upscaleAdvertisedPresentation())) && upscaleLegacyUnlisted(group)) {
        methods[advertised] = UpscaleMethod::AdvertisedMode;
    }
    return methods;
}

void upscaleSetGlobalMethods(const UpscaleMethods &methods)
{
    // Through the generated setters rather than the group, so that the page's
    // own save() writes these along with everything else it manages instead
    // of writing its cached values back over them.
    UpscaleConfig::setMethodWaylandFullScreen(upscaleMethodKey(methods[std::size_t(UpscalePresentation::WaylandFullScreen)]));
    UpscaleConfig::setMethodWaylandBorderless(upscaleMethodKey(methods[std::size_t(UpscalePresentation::WaylandBorderless)]));
    UpscaleConfig::setMethodWaylandWindowed(upscaleMethodKey(methods[std::size_t(UpscalePresentation::WaylandWindowed)]));
    UpscaleConfig::setMethodX11FullScreen(upscaleMethodKey(methods[std::size_t(UpscalePresentation::X11FullScreen)]));
    UpscaleConfig::setMethodX11Borderless(upscaleMethodKey(methods[std::size_t(UpscalePresentation::X11Borderless)]));
    UpscaleConfig::setMethodX11Windowed(upscaleMethodKey(methods[std::size_t(UpscalePresentation::X11Windowed)]));
}

UpscaleSettings upscaleGlobalSettings()
{
    UpscaleSettings settings;
    for (const UpscaleSettingInfo &info : upscaleSettingTable()) {
        settings.setValue(info.setting, info.global());
    }
    // Nothing claimed a window here, so the global profile's own switch is
    // what decides whether the effect acts on it.
    const KConfigGroup group = globalGroup();
    settings.setActs((UpscaleConfig::unlistedApplications() || upscaleLegacyUnlisted(group))
                     && !upscaleLegacySwitchedOff(group));
    return settings;
}

UpscaleSettings upscaleResolveSettings(const UpscaleApplication *claimed)
{
    UpscaleSettings settings = upscaleGlobalSettings();
    if (!claimed) {
        return settings;
    }
    for (const UpscaleSettingInfo &info : upscaleSettingTable()) {
        if (const std::optional<int> &stated = claimed->overrides[std::size_t(info.setting)]) {
            settings.setValue(info.setting, *stated);
        }
    }
    // Participation is the profile's own and is never inherited: a profile
    // that claimed this window answers for it, whatever the global profile
    // does about the windows nothing claimed.
    settings.setActs(claimed->enabled && !upscaleLegacySwitchedOff(globalGroup()));
    return settings;
}

} // namespace KWin
