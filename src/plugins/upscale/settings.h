/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "placement.h"
#include "presentation.h"
#include "resolution.h"

#include <array>
#include <optional>

class KConfigGroup;

namespace KWin
{

struct UpscaleApplication;

/**
 * Every preference the effect has, declared once.
 *
 * A preference says what the user wants. Each one has a global value and an
 * optional per-game override: a key present in a profile is that game's
 * answer, a key absent follows the global, and that is the whole rule.
 *
 * What is deliberately not here is anything describing a game rather than a
 * wish - its identity, the methods measured for it, and whether its profile
 * acts at all. None of those can be inherited, because a measurement of one
 * program says nothing about another, so they are fields on the profile and
 * not rows in this table.
 *
 * The table below is the single list. Storage reads and writes by enumerator,
 * the settings page builds one control per row, and the runtime asks a
 * resolved value for one by name. Adding a preference is one enumerator and
 * one row. The state this replaces had every reader deciding its own setting's
 * shape: a threshold meaning "inherit" at -1 in one file, a preset whose two
 * layers negotiated in another, and a percentage that only existed globally
 * while the preset naming it did not.
 */
enum class UpscaleSetting {
    Resolution,
    Percentage,
    MinimumPixels,
    Sharpening,
    Strength,
    ResolutionControl,
    Osd,
    OsdDetection,
    OsdSummary,
    OsdStatistics,
    OsdDeveloper,
    OsdTimeout,
    AnnouncementPosition,
    StatisticsPosition,
    DeveloperPosition,
};

inline constexpr std::size_t upscaleSettingCount = std::size_t(UpscaleSetting::DeveloperPosition) + 1;

/** What the settings page builds, and how a stored value is read back. */
enum class UpscaleSettingType {
    Switch,
    Number,
    Choice,
};

struct UpscaleSettingInfo
{
    UpscaleSetting setting;
    /** The configuration key. Both layers spell it the same way. */
    const char *key;
    UpscaleSettingType type;
    /** Inclusive bounds. A Switch is 0 or 1; a Choice indexes its list. */
    int minimum;
    int maximum;
    /**
     * The global layer's current value for this preference.
     *
     * A function per row, reading the generated configuration, rather than a
     * number repeated here. The defaults stay declared once in
     * upscaleconfig.kcfg, which is where KWin's own effects declare theirs and
     * what the settings page's Defaults button restores; this table says only
     * which entry belongs to which preference.
     */
    int (*global)();
    /**
     * How this preference is spelled in a configuration file, where a number
     * would not be readable.
     *
     * kwinupscalerc is meant to be read and edited by a person: its header
     * explains every field, and "Resolution=Quality" is a sentence where
     * "Resolution=2" is a puzzle whose answer changes if the enumeration ever
     * gains a value. Null for the preferences that are genuinely numbers,
     * which are stored as themselves.
     */
    QString (*name)(int value);
    int (*value)(const QString &name, int absent);
};

/** The table. One row per enumerator, in enumerator order. */
const std::array<UpscaleSettingInfo, upscaleSettingCount> &upscaleSettingTable();

/** The row for one preference. */
const UpscaleSettingInfo &upscaleSettingInfo(UpscaleSetting setting);

/**
 * What a profile states, for the preferences it states anything about.
 *
 * Absent means inherit. False and zero are values a person can choose and are
 * stored like any other, which is why this is an optional per entry rather
 * than a value compared against the global one: an explicit choice has to
 * survive the global value later changing to match it.
 */
using UpscaleSettingOverrides = std::array<std::optional<int>, upscaleSettingCount>;

/** Read a profile's overrides from its configuration group. */
UpscaleSettingOverrides upscaleReadOverrides(const KConfigGroup &group);

/**
 * Write @p overrides to @p group, storing only what differs from @p original.
 *
 * Everything else keeps following the installed package. A key the user set
 * back to what the package says is still written, because it expresses a
 * choice that has to survive the package changing its mind.
 */
void upscaleWriteOverrides(KConfigGroup &group, const UpscaleSettingOverrides &overrides,
                           const UpscaleSettingOverrides &original);

/**
 * Every preference resolved for one window, with nothing left to look up.
 *
 * Values are held as integers because that is what lets one table serve
 * storage, the settings page and the runtime alike; the accessors give them
 * back as what they mean. Nothing outside this class reads the array.
 */
class UpscaleSettings
{
public:
    /**
     * Whether the effect acts on this window at all.
     *
     * False where the profile that claimed the window is switched off, and
     * where nothing claimed it and the global profile is switched off.
     *
     * It does not silence the timed announcement. A window the effect looked
     * at and left alone is worth the one line that says so, because that line
     * is the only thing distinguishing being left alone from being broken.
     */
    bool acts() const;
    void setActs(bool acts);

    int value(UpscaleSetting setting) const;
    void setValue(UpscaleSetting setting, int value);

    bool switchedOn(UpscaleSetting setting) const;
    UpscaleCorner corner(UpscaleSetting setting) const;
    ResolutionPreset resolution() const;

    /** The sharpening strength the scaler wants, zero when switched off. */
    double sharpening() const;

private:
    bool m_acts = false;
    std::array<int, upscaleSettingCount> m_values{};
};

/**
 * The preferences for a window, from the global layer and @p claimed.
 *
 * @p claimed is the profile that matched the window, or null where nothing
 * did. It supplies the preferences it states, the global layer supplies the
 * rest, and a null profile takes the global layer entire.
 *
 * This reads configuration that has already been parsed, not the disk. It is
 * cheap enough to call when a window's identity changes and on reconfigure,
 * and it is never called while a frame is being painted.
 */
UpscaleSettings upscaleResolveSettings(const UpscaleApplication *claimed);

/**
 * The global profile's own six answers, for a window no profile claimed.
 *
 * Kept beside the preferences rather than with the profiles, because the
 * global profile is the one with no identity and so has no group of its own in
 * the catalogue; its entries live in kwinrc with the rest of the global layer.
 */
UpscaleMethods upscaleGlobalMethods();
void upscaleSetGlobalMethods(const UpscaleMethods &methods);

/** The global layer alone, for the settings page and for an unclaimed window. */
UpscaleSettings upscaleGlobalSettings();

} // namespace KWin
