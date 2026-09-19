/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "resolution.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QString>

// For wl_resource and the two interfaces below, declared by KWin itself.
#include "wayland/output.h"

namespace KWin
{

class ClientConnection;

/**
 * Tells one recognized application that its screen has a smaller current mode.
 *
 * A program decides the size of the image it renders before it has a window,
 * from the display information it was given when it connected. Measured on
 * KWin 6.3.6: once a game is running, neither a scale hint nor a rewritten
 * mode nor a smaller window makes it render less, and shrinking its window
 * only makes it scale its own finished image down. The one moment that still
 * changes the outcome is the moment the program binds the output, before it
 * enumerates displays, and that is the moment this class acts on.
 *
 * What it changes is deliberately narrow. The events go to the resources of
 * one client, so no other application, the output itself, the desktop scale
 * and the user's own game settings are all left exactly as they were, and
 * nothing has to be restarted: the effect is loaded long before a game starts.
 *
 * It is a statement, not enforcement. A program that ignores mode information,
 * or that asks the compositor for its fullscreen size instead of selecting a
 * mode, keeps rendering at its own size. Callers must therefore report what
 * was advertised separately from the buffer that actually arrived.
 */
class UpscaleModeOverride : public QObject
{
    Q_OBJECT

public:
    UpscaleModeOverride();
    ~UpscaleModeOverride() override;

    /**
     * Apply the current settings.
     *
     * @p preset and @p percentage are the global values. A recognized
     * application whose global preset is Automatic uses the size recorded for
     * it in the catalogue instead, so that installing the effect is enough for
     * a known game; an explicit global choice always wins over that.
     */
    void reconfigure(bool enabled, ResolutionPreset preset, int percentage);

    /** Whether this session has a Wayland server to talk to at all. */
    static bool available();

    /** The size last advertised to this program, or an invalid size. */
    QSize advertised(const QString &program) const;

private:
    // Whether the record of what each program was told goes back with the
    // resources. What a program was told stays true when the setting behind it
    // changes, and it is what explains the size that program is still
    // rendering, so it outlives the resources unless the effect stops asking.
    enum class Record {
        Keep,
        Discard,
    };

    void watchOutputs();
    void watchOutput(OutputInterface *output);
    void announce(OutputInterface *output, ClientConnection *client, wl_resource *resource);
    void restore(Record record = Record::Discard);

    // What one client is told: the buffer it should commit, and the integer
    // output scale that belongs with it, or zero where the size stands alone.
    struct Advertisement
    {
        QSize size;
        int scale = 0;
    };
    Advertisement advertisementFor(OutputInterface *output, const UpscaleApplication &application) const;

    // One client that was told a different mode, kept so that the real mode
    // can be sent back when the user turns this off. Both ends can disappear
    // first: a game exits, an output is unplugged.
    struct Announcement
    {
        QPointer<OutputInterface> output;
        QPointer<ClientConnection> client;
        QString program;
    };

    bool m_enabled = false;
    ResolutionPreset m_preset = ResolutionPreset::Automatic;
    int m_percentage = 100;
    QSet<const OutputInterface *> m_watched;
    QList<Announcement> m_announced;
    QHash<QString, QSize> m_advertised;
};

} // namespace KWin
