/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "resolution.h"

#include <KSharedConfig>

#include <QString>

#include <vector>

namespace KWin
{

/**
 * How the effect asks an application for a smaller image.
 *
 * The effect cannot make a program render differently. It can only change what
 * the program is told, and a program acts only on what it happens to read, so
 * every method names a specific thing said to a specific application rather
 * than a promise about the result. Which one applies was read in that
 * program's source and then confirmed by running it; it cannot be guessed from
 * the outside, because all of them look alike until the request is made.
 */
enum class UpscaleControlMethod {
    /** Say nothing. The supplied buffer is scaled at whatever size it arrives. */
    None,
    /**
     * Tell this application alone that its screen has a smaller current mode,
     * at the moment it binds the output and before it enumerates displays.
     *
     * For clients that select a display mode and then present it through a
     * viewport that still covers the screen, which is what SDL 2 does in
     * exclusive fullscreen. Any calculated size can be asked for.
     */
    AdvertisedMode,
    /**
     * Tell this application alone that its screen has a smaller scale.
     *
     * For clients that render the logical screen size multiplied by the scale
     * they were told and declare that scale on their surface. Their image
     * still covers the screen, because the compositor divides the buffer by
     * the scale the client declared. Only whole steps of the output's own
     * scale are reachable, so an unscaled screen offers such a client nothing.
     */
    AdvertisedScale,
    /**
     * Tell this application alone about both a smaller mode and a smaller
     * scale.
     *
     * For clients that take their fullscreen size from the mode in pixels but
     * declare the output's scale on their surface. Either alone leaves the two
     * disagreeing: the mode alone shrinks the window away from the screen
     * edges, and the scale alone stretches it past them.
     */
    AdvertisedModeAndScale,
    /** Resize a selected X11 client which follows resize events and RandR modes. */
    X11Resize,
};

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
    UpscaleControlMethod method = UpscaleControlMethod::None;
    /**
     * The resolution this application gets while the global preset is
     * Automatic. Native is an explicit opt-out even with a global preset.
     */
    ResolutionPreset preset = ResolutionPreset::Automatic;
    /** Physical output pixel threshold; -1 inherits the global setting. */
    int minimumPixels = -1;
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
 * The entry to use for an application the list does not describe, or null.
 *
 * Off unless the user asks for it. Nothing can be known in advance about a
 * program nobody measured, so this asks every client that connects for the
 * same thing and reports what each one did with it.
 */
const UpscaleApplication *upscaleUnknownApplication();

/** Whether unlisted applications are asked for a resolution at all. */
void upscaleSetUnknownApplications(bool enabled, ResolutionPreset preset);

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
QString upscaleMethodKey(UpscaleControlMethod method);

/** The configuration name of a preset, as the stored file spells it. */
QString upscalePresetKey(ResolutionPreset preset);

/** One sentence naming what the effect does for this application. */
QString describeControlMethod(UpscaleControlMethod method);

} // namespace KWin
