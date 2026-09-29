/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "logicalsize.h"

#include "compatibility.h"

#include "core/output.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include "wayland/output.h"
#include "wayland_server.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace KWin
{

// The events of zxdg_output_v1, numbered in the order the protocol declares
// them, which is the order they keep on the wire in every version.
static constexpr uint32_t logicalPositionEvent = 0;
static constexpr uint32_t logicalSizeEvent = 1;
static constexpr uint32_t doneEvent = 2;
// Since version 3 an xdg_output's changes are applied with its wl_output's
// done event, and its own done is deprecated.
static constexpr int outputDoneSince = 3;

static wl_display *display()
{
    return *waylandServer()->display();
}

// Sends one xdg_output a logical size and the done that applies it.
static void send(wl_resource *resource, OutputInterface *output, const QSize &size)
{
    wl_resource_post_event(resource, logicalSizeEvent, size.width(), size.height());
    if (wl_resource_get_version(resource) >= outputDoneSince) {
        output->done(wl_resource_get_client(resource));
    } else {
        wl_resource_post_event(resource, doneEvent);
    }
}

UpscaleLogicalSizes::UpscaleLogicalSizes() = default;

UpscaleLogicalSizes::~UpscaleLogicalSizes()
{
    restore();
}

void UpscaleLogicalSizes::tell(ClientConnection *client, OutputInterface *output, const QSize &logical)
{
    if (!client || !output || !output->handle()) {
        return;
    }
    auto known = std::ranges::find_if(m_clients, [client](const auto &entry) {
        return entry->connection == client;
    });
    if (known == m_clients.end()) {
        auto entry = std::make_unique<Client>();
        entry->created.listener.notify = &UpscaleLogicalSizes::created;
        entry->created.record = entry.get();
        entry->owner = this;
        entry->connection = client;
        wl_client_add_resource_created_listener(client->client(), &entry->created.listener);
        // KWin destroys the connection after libwayland has let go of the
        // client's list of listeners, so leaving it then is safe.
        connect(client, &QObject::destroyed, this, [this, raw = entry.get()]() {
            forget(raw);
        });
        m_clients.push_back(std::move(entry));
        known = std::prev(m_clients.end());
    }
    (*known)->logical.insert(output->handle()->name(), logical);
}

void UpscaleLogicalSizes::restore()
{
    if (m_idle) {
        wl_event_source_remove(m_idle);
        m_idle = nullptr;
    }
    if (m_logger) {
        wl_protocol_logger_destroy(m_logger);
        m_logger = nullptr;
    }
    for (const auto &resource : m_resources) {
        wl_list_remove(&resource->destroyed.listener.link);
        if (resource->output.isEmpty() || !waylandServer()) {
            continue;
        }
        // The size KWin itself sends: the output's, scaled for the client.
        const auto outputs = waylandServer()->display()->outputs();
        for (OutputInterface *output : outputs) {
            if (output->handle() && output->handle()->name() == resource->output) {
                const qreal scale = resource->client->connection->scaleOverride();
                const QSizeF size = output->handle()->geometryF().size() * scale;
                send(resource->resource, output, size.toSize());
            }
        }
    }
    m_resources.clear();
    for (const auto &client : m_clients) {
        wl_list_remove(&client->created.listener.link);
        disconnect(client->connection, nullptr, this, nullptr);
    }
    m_clients.clear();
}

void UpscaleLogicalSizes::forget(Client *client)
{
    std::erase_if(m_resources, [client](const auto &resource) {
        if (resource->client != client) {
            return false;
        }
        wl_list_remove(&resource->destroyed.listener.link);
        return true;
    });
    std::erase_if(m_clients, [client](const auto &entry) {
        if (entry.get() != client) {
            return false;
        }
        wl_list_remove(&entry->created.listener.link);
        return true;
    });
}

// Every object a told program creates passes here, as libwayland creates it
// and before KWin has sent anything on it. Only an xdg_output is of interest.
void UpscaleLogicalSizes::created(wl_listener *listener, void *data)
{
    auto client = static_cast<Client *>(reinterpret_cast<Hook *>(listener)->record);
    auto native = static_cast<wl_resource *>(data);
    if (std::strcmp(wl_resource_get_class(native), "zxdg_output_v1") != 0) {
        return;
    }
    auto resource = std::make_unique<Resource>();
    resource->destroyed.listener.notify = &UpscaleLogicalSizes::destroyed;
    resource->destroyed.record = resource.get();
    resource->owner = client->owner;
    resource->client = client;
    resource->resource = native;
    wl_resource_add_destroy_listener(native, &resource->destroyed.listener);
    client->owner->m_resources.push_back(std::move(resource));
    client->owner->watchFor(client->owner->m_resources.back().get());
}

void UpscaleLogicalSizes::destroyed(wl_listener *listener, void *data)
{
    Q_UNUSED(data)
    auto resource = static_cast<Resource *>(reinterpret_cast<Hook *>(listener)->record);
    wl_list_remove(&resource->destroyed.listener.link);
    std::erase_if(resource->owner->m_resources, [resource](const auto &entry) {
        return entry.get() == resource;
    });
}

// KWin sends a new xdg_output its position, its size, then its name and
// description where the version has them, and the done that applies them, all
// in the same request (6.3.6, 6.6.6 and master alike). Only for that moment is
// every event read.
void UpscaleLogicalSizes::watchFor(Resource *resource)
{
    Q_UNUSED(resource)
    if (!m_logger) {
        m_logger = wl_display_add_protocol_logger(display(), &UpscaleLogicalSizes::logged, this);
    }
    if (!m_idle) {
        m_idle = wl_event_loop_add_idle(wl_display_get_event_loop(display()), &UpscaleLogicalSizes::idle, this);
    }
}

// The position says which output the object stands for. The told size goes out
// as the event after KWin's size is logged, which libwayland does just before
// sending it, so it follows KWin's size and precedes the done that applies
// both: the program never applies KWin's size on its own. SDL 3.2.10 did, when
// it came with a done of its own, and divided by a density of two thirds cast
// to an integer on the told size after it (SDL_waylandvideo.c:1002): SIGFPE at
// start, on the bench of 2026-09-29.
void UpscaleLogicalSizes::logged(void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
{
    auto self = static_cast<UpscaleLogicalSizes *>(data);
    if (direction != WL_PROTOCOL_LOGGER_EVENT || self->m_sending) {
        return;
    }
    const auto found = std::ranges::find_if(self->m_resources, [message](const auto &resource) {
        return resource->resource == message->resource && resource->output.isEmpty();
    });
    if (found == self->m_resources.end()) {
        return;
    }
    Resource *resource = found->get();
    if (message->message_opcode == logicalPositionEvent && message->arguments_count == 2) {
        if (!resource->positioned) {
            resource->position = QPoint(message->arguments[0].i, message->arguments[1].i);
            resource->positioned = true;
        }
    } else if (message->message_opcode == logicalSizeEvent) {
        resource->sized = true;
    } else if (resource->sized) {
        self->m_sending = true;
        self->answer(false);
        self->m_sending = false;
    }
}

// Runs once libwayland has handled every request it read in this round, and
// before KWin flushes what it sent: after KWin's events on the new objects,
// in the same message to the program. An object the logger did not answer, if
// KWin ever sends its events otherwise, is answered here with a done of its own.
void UpscaleLogicalSizes::idle(void *data)
{
    auto self = static_cast<UpscaleLogicalSizes *>(data);
    // libwayland removes an idle source once it has run.
    self->m_idle = nullptr;
    if (self->m_logger) {
        wl_protocol_logger_destroy(self->m_logger);
        self->m_logger = nullptr;
    }
    self->answer(true);
}

// The output an object stands for, found by the position KWin sent on it,
// where that output was told a size; nothing otherwise.
OutputInterface *UpscaleLogicalSizes::toldOutput(const Resource &resource, const QList<OutputInterface *> &outputs)
{
    if (!resource.positioned) {
        return nullptr;
    }
    const qreal scale = resource.client->connection->scaleOverride();
    for (OutputInterface *output : outputs) {
        const UpscaleOutput *handle = output->handle();
        if (handle && (handle->geometryF().topLeft() * scale).toPoint() == resource.position
            && resource.client->logical.contains(handle->name())) {
            return output;
        }
    }
    return nullptr;
}

void UpscaleLogicalSizes::answer(bool done)
{
    const auto outputs = waylandServer()->display()->outputs();
    std::erase_if(m_resources, [&outputs, done](const auto &resource) {
        if (!resource->output.isEmpty() || (!done && !resource->sized)) {
            return false;
        }
        // An object whose position did not arrive, or whose output was not
        // told anything, is left as KWin described it.
        OutputInterface *output = toldOutput(*resource, outputs);
        if (!output) {
            wl_list_remove(&resource->destroyed.listener.link);
            return true;
        }
        resource->output = output->handle()->name();
        const QSize told = resource->client->logical.value(resource->output);
        if (done) {
            send(resource->resource, output, told);
        } else {
            wl_resource_post_event(resource->resource, logicalSizeEvent, told.width(), told.height());
        }
        return false;
    });
}

} // namespace KWin
