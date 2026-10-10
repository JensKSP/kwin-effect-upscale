/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace KWin
{

class ClientConnection;
class EffectWindow;
class EffectsHandler;
class Window;
struct UpscaleApplication;

/**
 * The name a program is matched by, from the @p executable path the system
 * resolved for it and the application ID @p sandbox its Wayland connection
 * declared, if any.
 *
 * Flatpak mounts an application at /app inside its sandbox, and that is the
 * path the system resolves for its program too, which tells one Flatpak from
 * another by nothing. So such a program is named with its application's ID in
 * the authority, as the session proxy names it:
 * flatpak://net.supertuxkart.SuperTuxKart/app/bin/supertuxkart. KWin keeps the
 * ID a sandbox declared for a connection but not which sandbox declared it, and
 * /app is where Flatpak, and no other, puts an application. Anything else is
 * named by its path.
 */
inline QString upscaleProgramName(const QString &executable, const QString &sandbox)
{
    if (sandbox.isEmpty() || !executable.startsWith(QLatin1String("/app/"))) {
        return executable;
    }
    return QStringLiteral("flatpak://") + sandbox + executable;
}

/** upscaleProgramName() of a Wayland @p client, from its credentials and its sandbox. */
QString upscaleProgramOf(const ClientConnection *client);

/**
 * The name of the program behind @p window, as upscaleProgramName() gives it
 * from the executable path, or empty.
 *
 * KWin resolves it from the window's PID. For a native Wayland client that
 * comes from its connection's credentials. For an X11 window KWin 6.3 reads
 * the _NET_WM_PID the client set, which the X server does not verify and
 * which a client in its own PID namespace fills with a number that means
 * another process here; by 6.6 KWin asks the X server, which knows the PID
 * from the client's connection, as for Wayland. A path that does not resolve
 * is empty, and an empty path matches no profile's gate 1. KWin's resolution
 * is the platform's own, so nothing here reads a process.
 *
 * This asks the system, so it is not for a frame: see
 * upscaleApplicationForWindow().
 */
QString upscaleExecutableOf(const Window *window);

/**
 * The application claiming @p window, or null.
 *
 * Eligibility asks this for every window on every frame, and matching now
 * resolves a path and runs patterns, neither of which belongs in a frame. So
 * the answer is kept per window: the path is resolved once, when the window
 * is first asked about, and the match is taken again only when the list is
 * read again or the window's class or instance changes - two string
 * comparisons per frame, which is what matching cost before there were
 * patterns. The window's geometry takes no part in which profile claims it,
 * so a window being moved or resized costs nothing here.
 */
const UpscaleApplication *upscaleApplicationForWindow(const Window *window);

/**
 * upscaleExecutableOf(@p window) as it was when the window was first asked
 * about, which is cheap enough for a frame; see upscaleApplicationForWindow().
 */
QString upscaleKnownExecutable(const Window *window);

/**
 * Whether @p window is a game's, as upscaleRecognizedGame() recognizes one
 * from its program, or upscaleGameDesktopFile() from the desktop entry the
 * window names, or as the X11 session proxy recognized it when its process
 * connected; see upscaleServedGame(). Kept per window as its program is, so cheap enough for a frame.
 */
bool upscaleGameWindow(const Window *window);

/**
 * The settings for @p window: those of the entry claiming it, which acts for
 * whatever it names, or else the global profile's, All games, which acts only
 * for a game's window; see upscaleGameWindow().
 */
UpscaleSettings upscaleSettingsForWindow(const Window *window);

/**
 * Tells the settings page which program a window belongs to.
 *
 * Add from Window picks a window through KWin, whose answer names its class
 * and instance but not, in KWin 6.3, its process. The effect can ask KWin, so
 * the page asks the effect: org.kde.KWin.Effect.Upscale1 at
 * /org/kde/KWin/Effect/Upscale1 on the session bus, the way KWin's own
 * effects export theirs. It returns only what any program of the same user
 * could already find out about that process.
 */
class UpscaleIdentityService : public QObject
{
    Q_OBJECT

public:
    explicit UpscaleIdentityService(QObject *parent = nullptr);

public Q_SLOTS:
    /** The executable path of the window with this internal ID, or empty. */
    QString executablePath(const QString &window) const;

    /**
     * Display policy for an authenticated X11 peer, before its first window.
     *
     * @p candidates names what the peer runs, most identifying first, as the
     * transport resolved it: the program behind a runtime that many programs
     * share, and the runtime itself. A profile matches when any of them
     * matches, so one pattern identifies a program whether it runs natively or
     * behind such a runtime. An empty list leaves the peer's own executable to
     * be resolved here, which is all a platform without that resolution has.
     */
    QVariantMap x11ConnectionPolicy(uint pid, const QStringList &candidates) const;

    /**
     * Whether a connection of Wine prefix @p prefix whose program is not known
     * yet is worth holding until it is: a pattern names this prefix, or
     * matches one of @p candidates. A pattern that could match in any prefix
     * holds none; the transport asks for the program's policy directly once
     * a launcher or the program itself names it.
     */
    bool x11PrefixMayMatch(const QString &prefix, const QStringList &candidates) const;

    /**
     * The transport showed process @p pid the screen it answered process
     * @p game with, as a process of the same Wine prefix; see
     * upscaleRecordShown().
     */
    void x11ProcessShown(uint game, uint pid);

    /**
     * The captions of the open windows an entry with these fields would match.
     *
     * The fields are named as the configuration file names them - Executable,
     * ExecutableMatch, WindowClass, WindowClassMatch, Instance, InstanceMatch -
     * so that the page can ask about an entry it has not stored yet. Matching
     * is the effect's own, gates and all; an entry with no usable gate
     * matches nothing. Asked when a person edits an entry, not per frame, so
     * resolving each window's path here is affordable.
     */
    QStringList windowsMatching(const QVariantMap &entry) const;

    /**
     * What the effect observed of the window with this internal ID, for the
     * report a person sends with an entry: its identity, how it presents, the
     * method and what it told or asked, the buffer, the output, and the
     * build, KWin and graphics it ran with. Empty for a window that is gone.
     * Sizes are width x height. The page makes the report of it, and keeps
     * the program's path to itself; see upscaleSubmissionReport().
     */
    QVariantMap reportFacts(const QString &window) const;

public:
    /** How the effect answers reportFacts(). */
    void setReporter(std::function<QVariantMap(EffectWindow *window)> reporter);
    /** What the effect does once x11ProcessShown() recorded a process. */
    void setShownHandler(std::function<void(uint pid)> handler);

private:
    // The compositor the effect was loaded into, which knows the windows.
    EffectsHandler *m_handler;
    std::function<QVariantMap(EffectWindow *)> m_reporter;
    std::function<void(uint)> m_shown;
};

} // namespace KWin
