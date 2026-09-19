/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>

#include <algorithm>
#include <ranges>

namespace KWin
{

// The effect's defaults are installed beside the session's other configuration
// defaults, and the user's changes land in their own file of the same name.
// KConfig reads both and lets the user's win field by field, which is what
// makes a package able to deliver a corrected method or a new application
// without disturbing anything the user edited.
static const char applicationFileName[] = "kwinupscalerc";
// Groups named for one application, so that the two layers describe the same
// entry. A generated identifier would not survive the file being shipped again.
static const QString applicationGroupPrefix = QStringLiteral("Application-");

KSharedConfig::Ptr upscaleApplicationConfig()
{
    return KSharedConfig::openConfig(QLatin1String(applicationFileName));
}

static UpscaleControlMethod readMethod(const QString &name)
{
    if (name == QLatin1String("AdvertisedMode")) {
        return UpscaleControlMethod::AdvertisedMode;
    }
    if (name == QLatin1String("AdvertisedScale")) {
        return UpscaleControlMethod::AdvertisedScale;
    }
    if (name == QLatin1String("AdvertisedModeAndScale")) {
        return UpscaleControlMethod::AdvertisedModeAndScale;
    }
    // Includes "None" and anything a later version of this file may name.
    // Recognizing the application while asking it for nothing is the safe
    // reading of a method this build does not implement.
    return UpscaleControlMethod::None;
}

QString upscaleMethodKey(UpscaleControlMethod method)
{
    switch (method) {
    case UpscaleControlMethod::AdvertisedMode:
        return QStringLiteral("AdvertisedMode");
    case UpscaleControlMethod::AdvertisedScale:
        return QStringLiteral("AdvertisedScale");
    case UpscaleControlMethod::AdvertisedModeAndScale:
        return QStringLiteral("AdvertisedModeAndScale");
    case UpscaleControlMethod::None:
        break;
    }
    return QStringLiteral("None");
}

static const QHash<QString, ResolutionPreset> &presetNames()
{
    static const QHash<QString, ResolutionPreset> s_names = {
        {QStringLiteral("Native"), ResolutionPreset::Native},
        {QStringLiteral("UltraQuality"), ResolutionPreset::UltraQuality},
        {QStringLiteral("Quality"), ResolutionPreset::Quality},
        {QStringLiteral("Balanced"), ResolutionPreset::Balanced},
        {QStringLiteral("Performance"), ResolutionPreset::Performance},
        {QStringLiteral("Custom"), ResolutionPreset::Custom},
    };
    return s_names;
}

QString upscalePresetKey(ResolutionPreset preset)
{
    return presetNames().key(preset, QStringLiteral("Automatic"));
}

static ResolutionPreset readPreset(const QString &name)
{
    // Automatic asks for nothing, so it is also what an unreadable name gets.
    return presetNames().value(name, ResolutionPreset::Automatic);
}

static std::vector<UpscaleApplication> readApplications(const KSharedConfig::Ptr &config)
{
    std::vector<UpscaleApplication> applications;
    const QStringList groups = config->groupList();
    for (const QString &name : groups) {
        if (!name.startsWith(applicationGroupPrefix)) {
            continue;
        }
        const KConfigGroup group(config, name);
        UpscaleApplication application;
        application.id = name.mid(applicationGroupPrefix.size());
        application.name = group.readEntry("Name", application.id);
        application.version = group.readEntry("MeasuredVersion", QString());
        application.windowClass = group.readEntry("WindowClass", QString());
        application.instance = group.readEntry("Instance", QString());
        application.program = group.readEntry("Program", QString());
        application.method = readMethod(group.readEntry("Method", QString()));
        application.preset = readPreset(group.readEntry("Preset", QString()));
        application.note = group.readEntry("Note", QString());
        application.order = group.readEntry("Order", 0);
        application.enabled = group.readEntry("Enabled", true);
        // An entry the effect's own defaults still describe. A user's addition
        // has no default behind it, which is how restoring tells the two apart.
        application.shipped = group.hasDefault("Name") || group.hasDefault("Method");
        // An entry that constrains no identity would match every window,
        // including the desktop. Dropping it is the only safe reading.
        if (application.windowClass.isEmpty() && application.instance.isEmpty()) {
            continue;
        }
        applications.push_back(std::move(application));
    }
    // A stable order, so that the first match is the same on every start. The
    // identifier breaks ties rather than leaving it to the file's layout.
    std::ranges::sort(applications, [](const UpscaleApplication &first, const UpscaleApplication &second) {
        return std::tie(first.order, first.id) < std::tie(second.order, second.id);
    });
    return applications;
}

// Read once and kept: matching runs whenever a window appears or a report is
// assembled, and neither may touch the disk. upscaleReloadApplications() is
// what a reconfiguration calls to pick up an edit.
static std::optional<std::vector<UpscaleApplication>> cachedApplications;

const std::vector<UpscaleApplication> &upscaleApplications()
{
    if (!cachedApplications) {
        cachedApplications = readApplications(upscaleApplicationConfig());
    }
    return *cachedApplications;
}

void upscaleReloadApplications()
{
    const KSharedConfig::Ptr config = upscaleApplicationConfig();
    config->reparseConfiguration();
    cachedApplications = readApplications(config);
}

bool upscaleApplicationsCustomized()
{
    // Only the user's own file, without the defaults underneath it: a group
    // here is something they added or a field they changed.
    const KConfig user(QLatin1String(applicationFileName), KConfig::SimpleConfig);
    const QStringList groups = user.groupList();
    return std::ranges::any_of(groups, [](const QString &name) {
        return name.startsWith(applicationGroupPrefix);
    });
}

void upscaleRestoreApplications()
{
    const KSharedConfig::Ptr config = upscaleApplicationConfig();
    const QStringList groups = config->groupList();
    for (const QString &name : groups) {
        if (!name.startsWith(applicationGroupPrefix)) {
            continue;
        }
        KConfigGroup group(config, name);
        const QStringList keys = group.keyList();
        for (const QString &key : keys) {
            if (group.hasDefault(key)) {
                // Removes the user's value so the installed one shows through.
                // Deleting the key instead would write a marker that hides the
                // default, which is the opposite of restoring it.
                group.revertToDefault(key);
            } else {
                group.deleteEntry(key);
            }
        }
        // Whatever is left has no default behind it, so the user added it.
        if (group.keyList().isEmpty() && !group.hasDefault(QStringLiteral("Name"))) {
            group.deleteGroup();
        }
    }
    config->sync();
    upscaleReloadApplications();
}

// The application used for a program nobody measured, when the user has asked
// for that. It is not stored: it describes a setting, not an entry, and it
// must never appear in the list the editor writes back.
static std::optional<UpscaleApplication> unknownApplication;

void upscaleSetUnknownApplications(bool enabled, ResolutionPreset preset)
{
    if (!enabled) {
        unknownApplication.reset();
        return;
    }
    UpscaleApplication application;
    application.name = i18n("Unlisted application");
    // The mode is the one request that was observed to leave a client's window
    // covering the screen whether or not the client acts on it. A scale told
    // to a program that reads it differently moves its window off the screen,
    // which is not something to do to an application nobody measured.
    application.method = UpscaleControlMethod::AdvertisedMode;
    application.preset = preset;
    unknownApplication = application;
}

const UpscaleApplication *upscaleUnknownApplication()
{
    return unknownApplication ? &*unknownApplication : nullptr;
}

QString upscaleNewApplicationId(const QString &name, const std::vector<UpscaleApplication> &pending)
{
    // Readable, stable, and free of anything a configuration group cannot
    // hold. A name that reduces to nothing still needs an identifier.
    QString id;
    for (const QChar character : name.toLower()) {
        if (character.isLetterOrNumber()) {
            id.append(character);
        }
    }
    if (id.isEmpty()) {
        id = QStringLiteral("application");
    }
    const std::vector<UpscaleApplication> &existing = upscaleApplications();
    const auto taken = [&existing, &pending](const QString &candidate) {
        const auto same = [&candidate](const UpscaleApplication &other) {
            return other.id == candidate;
        };
        return std::ranges::any_of(existing, same) || std::ranges::any_of(pending, same);
    };
    QString candidate = id;
    for (int suffix = 2; taken(candidate); ++suffix) {
        candidate = id + QString::number(suffix);
    }
    return candidate;
}

// Store a field only where it differs from what is already in effect, so that
// everything else keeps following the installed package.
static void writeField(KConfigGroup &group, const char *key, const QString &value, const QString &original)
{
    if (value != original) {
        group.writeEntry(key, value);
    }
}

void upscaleSaveApplication(const UpscaleApplication &application, const UpscaleApplication &original)
{
    const KSharedConfig::Ptr config = upscaleApplicationConfig();
    KConfigGroup group(config, applicationGroupPrefix + application.id);
    writeField(group, "Name", application.name, original.name);
    writeField(group, "WindowClass", application.windowClass, original.windowClass);
    writeField(group, "Instance", application.instance, original.instance);
    writeField(group, "Program", application.program, original.program);
    writeField(group, "Method", upscaleMethodKey(application.method), upscaleMethodKey(original.method));
    writeField(group, "Preset", upscalePresetKey(application.preset), upscalePresetKey(original.preset));
    if (application.enabled != original.enabled) {
        group.writeEntry("Enabled", application.enabled);
    }
    if (application.order != original.order) {
        group.writeEntry("Order", application.order);
    }
    // A new entry has nothing behind it, so it carries its own description.
    if (!application.shipped && !application.note.isEmpty() && application.note != original.note) {
        group.writeEntry("Note", application.note);
    }
}

void upscaleDeleteApplication(const QString &id)
{
    const KSharedConfig::Ptr config = upscaleApplicationConfig();
    KConfigGroup group(config, applicationGroupPrefix + id);
    // Only an entry the effect does not ship can go away. Removing a shipped
    // one here would achieve nothing: the next package brings it back.
    if (group.hasDefault(QStringLiteral("Name")) || group.hasDefault(QStringLiteral("Method"))) {
        group.writeEntry("Enabled", false);
        return;
    }
    group.deleteGroup();
}

void upscaleSyncApplications()
{
    upscaleApplicationConfig()->sync();
    upscaleReloadApplications();
}

static bool matches(const UpscaleApplication &application, const QString &windowClass, const QString &instance)
{
    if (!application.enabled) {
        return false;
    }
    if (!application.windowClass.isEmpty() && application.windowClass != windowClass) {
        return false;
    }
    return application.instance.isEmpty() || application.instance == instance;
}

const UpscaleApplication *upscaleApplicationForIdentity(const QString &windowClass, const QString &instance)
{
    if (windowClass.isEmpty() && instance.isEmpty()) {
        // Identity can arrive after the window does. Nothing matches yet, and
        // windowClassChanged will bring the effect back here when it does.
        return nullptr;
    }
    for (const UpscaleApplication &application : upscaleApplications()) {
        if (matches(application, windowClass, instance)) {
            return &application;
        }
    }
    return nullptr;
}

const UpscaleApplication *upscaleApplicationForProgram(const QString &executablePath)
{
    if (executablePath.isEmpty()) {
        return nullptr;
    }
    // Compare the file name only: the same program sits in a different
    // directory depending on how it was installed.
    const QString program = executablePath.section(QLatin1Char('/'), -1);
    if (program.isEmpty()) {
        return nullptr;
    }
    for (const UpscaleApplication &application : upscaleApplications()) {
        if (application.program.isEmpty() || application.program != program) {
            continue;
        }
        // A listed application the user switched off is not an unlisted one.
        // Falling through to the entry below would ask it for a resolution
        // anyway, which is the opposite of what switching it off means.
        return application.enabled ? &application : nullptr;
    }
    // Only once nothing in the list describes this program, so that neither a
    // measured entry nor a deliberate refusal is replaced by a guess.
    return upscaleUnknownApplication();
}

QString describeControlMethod(UpscaleControlMethod method)
{
    switch (method) {
    case UpscaleControlMethod::None:
        return i18n("no resolution request");
    case UpscaleControlMethod::AdvertisedMode:
        return i18n("advertised screen mode");
    case UpscaleControlMethod::AdvertisedScale:
        return i18n("advertised screen scale");
    case UpscaleControlMethod::AdvertisedModeAndScale:
        return i18n("advertised screen mode and scale");
    }
    return QString();
}

} // namespace KWin
