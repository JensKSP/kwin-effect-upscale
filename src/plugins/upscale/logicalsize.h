/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>

#include <memory>
#include <vector>

#include <wayland-server-core.h>

namespace KWin
{
class ClientConnection;
class OutputInterface;

/**
 * Tells a program the logical size of an output that belongs with the smaller
 * mode it was told, through the program's xdg_output for that output.
 *
 * A program told a smaller wl_output mode still hears the output's true
 * logical size from its xdg_output. SDL3 reads the difference as a density:
 * it scales the program by the ratio and lists the true desktop size beside
 * the told mode, which a game may then pick. KWin offers no hook where a
 * program creates its xdg_output, as it does for wl_output, so this watches
 * the program's new objects for one, learns which output it stands for from
 * the position KWin sends on it, and sends the told size after KWin's own
 * events, before they leave for the program. restore() sends the true size.
 */
class UpscaleLogicalSizes : public QObject
{
public:
    UpscaleLogicalSizes();
    ~UpscaleLogicalSizes() override;

    /** From now on, @p client's xdg_output for @p output reports @p logical. */
    void tell(ClientConnection *client, OutputInterface *output, const QSize &logical);
    /** Sends every xdg_output this told its output's true logical size again, and forgets them. */
    void restore();

private:
    // A libwayland listener with the record it belongs to. Standard layout, so
    // the listener's address is the hook's.
    struct Hook
    {
        wl_listener listener;
        void *record;
    };
    // A program this has told something, watched for the objects it creates.
    struct Client
    {
        Hook created;
        UpscaleLogicalSizes *owner;
        ClientConnection *connection;
        // Logical sizes by output name.
        QHash<QString, QSize> logical;
    };
    // One of its xdg_output objects: first waiting for the position KWin
    // sends on it, then told, and kept until it is given back or goes away.
    struct Resource
    {
        Hook destroyed;
        UpscaleLogicalSizes *owner;
        Client *client;
        wl_resource *resource;
        QPoint position;
        bool positioned = false;
        QString output;
    };

    static void created(wl_listener *listener, void *data);
    static void destroyed(wl_listener *listener, void *data);
    static void logged(void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message);
    static void idle(void *data);
    void answer();
    void forget(Client *client);
    void watchFor(Resource *resource);

    std::vector<std::unique_ptr<Client>> m_clients;
    std::vector<std::unique_ptr<Resource>> m_resources;
    wl_protocol_logger *m_logger = nullptr;
    wl_event_source *m_idle = nullptr;
};

} // namespace KWin
