/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "pattern.h"
#include "presentation.h"
#include "resolution.h"
#include "settings.h"

#include <KSharedConfig>

#include <QString>

#include <vector>

namespace KWin
{

/**
 * One application the effect recognizes, as it was read from configuration.
 *
 * The effect ships these as its own defaults and never writes to them. A user's
 * entries and changes are layered over them by KConfig, so a field nobody
 * changed keeps following the installed package.
 */
struct UpscaleApplication
{
    /** The configuration group's stable name, which both layers share. */
    QString id;
    /** Shown to the user; not used for matching. */
    QString name;
    /** The package this identity was read from, so a later mismatch is traceable. */
    QString version;
    /**
     * Gate 1: the executable path of the program behind the window, or empty
     * to not constrain it.
     *
     * KWin resolves it, from the connection's own credentials for a native
     * Wayland client and from the window's PID for an X11 one. The shipped
     * entries state a regular expression matching the file name in any
     * directory, because a game is installed in different places; a person's
     * own entry may state the exact path of their copy.
     */
    QString executable;
    UpscaleStringMatch executableMatch = UpscaleStringMatch::Exact;
    /** Gate 2: compared with Window::resourceClass(), or empty to not constrain it. */
    QString windowClass;
    UpscaleStringMatch windowClassMatch = UpscaleStringMatch::Exact;
    /** Gate 2: compared with Window::resourceName(), or empty to not constrain it. */
    QString instance;
    UpscaleStringMatch instanceMatch = UpscaleStringMatch::Exact;
    /**
     * What to say to this program in each way it can present itself.
     *
     * One answer per presentation, because the request that works is a
     * property of how the program is running and not only of the program: the
     * advertisements act on wl_output, which an Xwayland game never sees, and
     * the resize acts on an X11 window.
     *
     * A slot the entry states nothing for follows the global profile's answer,
     * which is Auto unless a person chose another; upscaleMethodFor() resolves
     * it. Before Auto existed a method was only ever a measurement, and a game
     * inherited nothing; with Auto there is a sensible answer to inherit.
     */
    UpscaleStatedMethods methods{};
    /**
     * What the installed package states for each slot, empty for an entry the
     * package does not ship. What the settings page returns a slot to when a
     * person resets it: the package's measurement where there is one, and
     * otherwise nothing, which follows the global profile.
     */
    UpscaleStatedMethods measured{};
    /**
     * The preferences this profile states, of those in the settings table.
     *
     * Absent means the global value applies. There is no sentinel and no
     * negotiation: a key present here is this game's answer, whatever the
     * global layer says and whether or not the two happen to agree.
     */
    UpscaleSettingOverrides overrides;
    /** Why this entry looks the way it does, for the settings page. */
    QString note;
    /** Matching order; the first enabled match wins as a whole. */
    int order = 0;
    /** A user can stop an entry matching without deleting it. */
    bool enabled = true;
    /** Whether the effect's own defaults still describe this entry. */
    bool shipped = false;
    /** Refuse secondary outputs for clients whose own mode API selects primary. */
    bool x11PrimaryOutputOnly = false;
    /** Require a client-selected mode as evidence that it handled the resize. */
    bool x11RequiresEmulatedMode = false;
};

/** The applications this session recognizes, in matching order. */
const std::vector<UpscaleApplication> &upscaleApplications();

/**
 * How often the list has been read, so that anything derived from it knows
 * when to derive it again. It is at least one once the list has been read.
 */
quint64 upscaleApplicationsGeneration();

/**
 * Read the applications again.
 *
 * Reading configuration is disk work, so it happens when the effect is
 * reconfigured and never while a frame is being painted.
 */
void upscaleReloadApplications();

/** The file holding the effect's own defaults and the user's changes to them. */
/**
 * The method @p application uses when presenting as @p presentation: its own
 * where it states one, the global profile's otherwise, and the global
 * profile's for a window no entry claimed, which is what a null entry means.
 */
UpscaleMethod upscaleMethodFor(const UpscaleApplication *application, UpscalePresentation presentation);

KSharedConfig::Ptr upscaleApplicationConfig();

/**
 * Whether the user's own file describes any application at all.
 *
 * Read from that file alone rather than from the layered result, because the
 * question is what there is to restore, not what the effect currently sees.
 */
bool upscaleApplicationsCustomized();

/**
 * Discard every change the user made to the application list.
 *
 * Fields the user overrode go back to what the installed package says, and
 * applications the user added are removed, so that afterwards the list is the
 * one this build ships and follows later packages again.
 */
void upscaleRestoreApplications();

/**
 * Store one application, writing only the fields that differ from @p original.
 *
 * Everything else keeps following the installed package. A field the user set
 * back to what the package says is still stored, because it expresses a choice
 * that has to survive the package changing its mind.
 */
void upscaleSaveApplication(const UpscaleApplication &application, const UpscaleApplication &original);

/**
 * Write @p applications to the file at @p path, every field of each, replacing
 * the file. The format is kwinupscalerc's own, so the file can be read back
 * with upscaleReadApplicationFile() or dropped in as someone's own list.
 */
bool upscaleWriteApplicationFile(const std::vector<UpscaleApplication> &applications, const QString &path);

/**
 * The applications a file at @p path describes, read exactly as the list is:
 * an entry that constrains no identity is dropped, and a previous release's
 * keys are read under their old meaning.
 */
std::vector<UpscaleApplication> upscaleReadApplicationFile(const QString &path);

/**
 * Remove one application the user added.
 *
 * An application the effect ships cannot be removed, because the next package
 * would bring it back; it is disabled instead, which the user's file records.
 */
void upscaleDeleteApplication(const QString &id);

/** Write the pending changes to disk. */
void upscaleSyncApplications();

/**
 * The identifier to give a new application, derived from @p name.
 *
 * @p pending are entries that are not stored yet, which an editor holding
 * unapplied changes has. Both layers are consulted, because two entries
 * sharing an identifier would share a configuration group and one of them
 * would be written over the other the moment they were applied.
 */
QString upscaleNewApplicationId(const QString &name, const std::vector<UpscaleApplication> &pending = {});

/** The configuration name of a method, as the stored file spells it. */
QString upscaleMethodKey(UpscaleMethod method);

/** A method from the name a file spells, or @p absent where it says nothing. */
UpscaleMethod upscaleMethodFromKey(const QString &name, UpscaleMethod absent);

/** The configuration key for one presentation's answer, in either layer. */
const char *upscalePresentationKey(UpscalePresentation presentation);

/** The configuration name of a preset, as the stored file spells it. */
QString upscalePresetKey(ResolutionPreset preset);

/** A preset from the name a file spells, or @p absent where it is unknown. */
ResolutionPreset upscalePresetFromKey(const QString &name, ResolutionPreset absent);

/** One sentence naming what the effect does for this application. */
QString describeControlMethod(UpscaleMethod method);

} // namespace KWin
