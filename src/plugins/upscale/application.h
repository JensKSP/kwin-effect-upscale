/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

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
    /** Exact match against Window::resourceClass(), or empty to not constrain it. */
    QString windowClass;
    /** Exact match against Window::resourceName(), or empty to not constrain it. */
    QString instance;
    /**
     * Exact match against the file name of ClientConnection::executablePath().
     * Used by Wayland advertisement methods before the application has a
     * window. X11 resizing uses the window identity instead.
     */
    QString program;
    /**
     * What to say to this program in each way it can present itself.
     *
     * One answer per presentation, because the request that works is a
     * property of how the program is running and not only of the program: the
     * advertisements act on wl_output, which an Xwayland game never sees, and
     * the resize acts on an X11 window. A slot nobody has measured holds Auto,
     * which is also what an absent key reads as.
     */
    UpscaleMethods methods{};
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
};

/** The applications this session recognizes, in matching order. */
const std::vector<UpscaleApplication> &upscaleApplications();

/**
 * Read the applications again.
 *
 * Reading configuration is disk work, so it happens when the effect is
 * reconfigured and never while a frame is being painted.
 */
void upscaleReloadApplications();

/** The file holding the effect's own defaults and the user's changes to them. */
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
 * The application matching a window class and instance name, or null.
 *
 * Fields left empty in an entry do not constrain the match; every field it
 * does state has to be equal, case included. Window titles are never used:
 * they change while a game is running. This takes the two strings rather than
 * a window so that the matching rules can be tested without a compositor, and
 * so that the settings module can match without KWin's effect interfaces.
 */
const UpscaleApplication *upscaleApplicationForIdentity(const QString &windowClass, const QString &instance);

/**
 * The application for a program path, matched on the file name alone.
 *
 * The directory is deliberately ignored: the same game is at /usr/games on
 * Debian and elsewhere in a Flatpak or a user build, and the file name is the
 * part that stayed the same. An empty path matches nothing.
 */
const UpscaleApplication *upscaleApplicationForProgram(const QString &executablePath);

/**
 * Store one application, writing only the fields that differ from @p original.
 *
 * Everything else keeps following the installed package. A field the user set
 * back to what the package says is still stored, because it expresses a choice
 * that has to survive the package changing its mind.
 */
void upscaleSaveApplication(const UpscaleApplication &application, const UpscaleApplication &original);

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
