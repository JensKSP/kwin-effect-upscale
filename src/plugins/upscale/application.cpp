/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"

#include "legacysettings.h"

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

UpscaleMethod upscaleMethodFromKey(const QString &name, UpscaleMethod absent)
{
    static const QHash<QString, UpscaleMethod> s_names = {
        {QStringLiteral("Auto"), UpscaleMethod::Auto},
        {QStringLiteral("Off"), UpscaleMethod::Off},
        {QStringLiteral("AdvertisedMode"), UpscaleMethod::AdvertisedMode},
        {QStringLiteral("AdvertisedScale"), UpscaleMethod::AdvertisedScale},
        {QStringLiteral("AdvertisedModeAndScale"), UpscaleMethod::AdvertisedModeAndScale},
        {QStringLiteral("X11Resize"), UpscaleMethod::X11Resize},
        // What "ask for nothing" was called before there was an Auto to tell
        // it apart from "nobody has measured this yet".
        {QStringLiteral("None"), UpscaleMethod::Off},
    };
    if (name.isEmpty()) {
        return absent;
    }
    // A name this build does not know comes from a later version of the file.
    // Asking for nothing is the safe reading: acting on a method we have not
    // implemented is the one outcome that can move a window off the screen.
    return s_names.value(name, UpscaleMethod::Off);
}

QString upscaleMethodKey(UpscaleMethod method)
{
    switch (method) {
    case UpscaleMethod::Auto:
        return QStringLiteral("Auto");
    case UpscaleMethod::AdvertisedMode:
        return QStringLiteral("AdvertisedMode");
    case UpscaleMethod::AdvertisedScale:
        return QStringLiteral("AdvertisedScale");
    case UpscaleMethod::AdvertisedModeAndScale:
        return QStringLiteral("AdvertisedModeAndScale");
    case UpscaleMethod::X11Resize:
        return QStringLiteral("X11Resize");
    case UpscaleMethod::Off:
        break;
    }
    return QStringLiteral("Off");
}

// The configuration key for one presentation's answer. Both layers spell
// these the same way, which is why the global profile's entries in
// upscaleconfig.kcfg carry these names too.
const char *upscalePresentationKey(UpscalePresentation presentation)
{
    switch (presentation) {
    case UpscalePresentation::WaylandFullScreen:
        return "MethodWaylandFullScreen";
    case UpscalePresentation::WaylandBorderless:
        return "MethodWaylandBorderless";
    case UpscalePresentation::WaylandWindowed:
        return "MethodWaylandWindowed";
    case UpscalePresentation::X11FullScreen:
        return "MethodX11FullScreen";
    case UpscalePresentation::X11Borderless:
        return "MethodX11Borderless";
    case UpscalePresentation::X11Windowed:
        return "MethodX11Windowed";
    }
    return "MethodWaylandFullScreen";
}

// The six answers, and the one answer a previous release stored.
//
// That release had a single Method per profile, which was necessarily a
// measurement of one presentation: an advertisement can only have been
// observed on a native Wayland client, and a resize on an X11 one, and both
// were only ever tried against a game presenting full screen. Each therefore
// has exactly one slot it can have come from. Off is the exception and fills
// every slot, because it recorded a decision about the program rather than
// about one way of running it.
static UpscaleMethods readMethods(const KConfigGroup &group)
{
    UpscaleMethods methods;
    methods.fill(UpscaleMethod::Auto);
    const UpscaleMethod single = upscaleMethodFromKey(group.readEntry("Method", QString()), UpscaleMethod::Auto);
    if (single == UpscaleMethod::Off) {
        methods.fill(UpscaleMethod::Off);
    } else if (single != UpscaleMethod::Auto) {
        const UpscalePresentation migrated = single == UpscaleMethod::X11Resize
            ? UpscalePresentation::X11FullScreen
            : UpscalePresentation::WaylandFullScreen;
        methods[std::size_t(migrated)] = single;
    }
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const auto presentation = UpscalePresentation(slot);
        const QString stated = group.readEntry(upscalePresentationKey(presentation), QString());
        if (!stated.isEmpty()) {
            methods[slot] = upscaleMethodFromKey(stated, methods[slot]);
        }
        // A method the presentation cannot carry is a file naming an
        // advertisement for an X11 window, or a resize for a Wayland one.
        // Neither can be acted on, and neither is worth refusing the whole
        // entry over.
        if (!upscaleMethodApplies(presentation, methods[slot])) {
            methods[slot] = UpscaleMethod::Off;
        }
    }
    return methods;
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

ResolutionPreset upscalePresetFromKey(const QString &name, ResolutionPreset absent)
{
    // A name this build does not know is a file from a later version. Falling
    // back to what the caller would have used anyway is the honest reading:
    // it neither invents a resolution nor refuses the whole entry.
    return presetNames().value(name, absent);
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
        application.executable = group.readEntry("Executable", QString());
        application.executableMatch = upscaleStringMatchFromKey(group.readEntry("ExecutableMatch", QString()));
        application.windowClass = group.readEntry("WindowClass", QString());
        application.windowClassMatch = upscaleStringMatchFromKey(group.readEntry("WindowClassMatch", QString()));
        application.instance = group.readEntry("Instance", QString());
        application.instanceMatch = upscaleStringMatchFromKey(group.readEntry("InstanceMatch", QString()));
        application.methods = readMethods(group);
        // After the methods: what an old Program key meant depended on them.
        upscaleReadLegacyProgram(group, application);
        application.overrides = upscaleReadOverrides(group);
        application.note = group.readEntry("Note", QString());
        application.order = group.readEntry("Order", 0);
        application.enabled = group.readEntry("Enabled", true);
        application.x11PrimaryOutputOnly = group.readEntry("X11PrimaryOutputOnly", false);
        // An entry the effect's own defaults still describe. A user's addition
        // has no default behind it, which is how restoring tells the two apart.
        application.shipped = group.hasDefault("Name") || group.hasDefault("Method")
            || group.hasDefault(upscalePresentationKey(UpscalePresentation::WaylandFullScreen))
            || group.hasDefault(upscalePresentationKey(UpscalePresentation::X11FullScreen));
        // An entry that constrains no identity would match every window,
        // including the desktop. Dropping it is the only safe reading.
        if (application.executable.isEmpty() && application.windowClass.isEmpty() && application.instance.isEmpty()) {
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
static quint64 applicationsGeneration = 0;

const std::vector<UpscaleApplication> &upscaleApplications()
{
    if (!cachedApplications) {
        cachedApplications = readApplications(upscaleApplicationConfig());
        ++applicationsGeneration;
    }
    return *cachedApplications;
}

quint64 upscaleApplicationsGeneration()
{
    return applicationsGeneration;
}

void upscaleReloadApplications()
{
    const KSharedConfig::Ptr config = upscaleApplicationConfig();
    config->reparseConfiguration();
    cachedApplications = readApplications(config);
    ++applicationsGeneration;
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
    writeField(group, "Executable", application.executable, original.executable);
    writeField(group, "ExecutableMatch", upscaleStringMatchKey(application.executableMatch),
               upscaleStringMatchKey(original.executableMatch));
    writeField(group, "WindowClass", application.windowClass, original.windowClass);
    writeField(group, "WindowClassMatch", upscaleStringMatchKey(application.windowClassMatch),
               upscaleStringMatchKey(original.windowClassMatch));
    writeField(group, "Instance", application.instance, original.instance);
    writeField(group, "InstanceMatch", upscaleStringMatchKey(application.instanceMatch),
               upscaleStringMatchKey(original.instanceMatch));
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const char *key = upscalePresentationKey(UpscalePresentation(slot));
        writeField(group, key, upscaleMethodKey(application.methods[slot]), upscaleMethodKey(original.methods[slot]));
    }
    // What a previous release stored under its own keys, rewritten under the
    // current ones before those keys go. See upscaleRetireLegacyProfileKeys()
    // for why simply deleting them would lose the values.
    upscaleRetireLegacyProfileKeys(group, application);
    upscaleWriteOverrides(group, application.overrides, original.overrides);
    if (application.enabled != original.enabled) {
        group.writeEntry("Enabled", application.enabled);
    }
    if (application.x11PrimaryOutputOnly != original.x11PrimaryOutputOnly) {
        group.writeEntry("X11PrimaryOutputOnly", application.x11PrimaryOutputOnly);
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

QString describeControlMethod(UpscaleMethod method)
{
    // Each names what is asked of the client, never what happens to the
    // window: the window keeps the place and size the system gave it, and
    // what the effect decides is the size the client renders at.
    switch (method) {
    case UpscaleMethod::X11Resize:
        return i18n("X11 buffer request");
    case UpscaleMethod::Off:
        return i18n("no resolution request");
    case UpscaleMethod::AdvertisedMode:
        return i18n("advertised screen mode");
    case UpscaleMethod::AdvertisedScale:
        return i18n("advertised screen scale");
    case UpscaleMethod::AdvertisedModeAndScale:
        return i18n("advertised screen mode and scale");
    case UpscaleMethod::Auto:
        return i18n("automatic, verified against what the application commits");
    }
    return QString();
}

} // namespace KWin
