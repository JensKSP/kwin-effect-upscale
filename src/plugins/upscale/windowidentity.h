/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace KWin
{

class EffectsHandler;
class Window;
struct UpscaleApplication;

/**
 * The executable path of the program behind @p window, or empty.
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

private:
    // The compositor the effect was loaded into, which knows the windows.
    EffectsHandler *m_handler;
};

} // namespace KWin
