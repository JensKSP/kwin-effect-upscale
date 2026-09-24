/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "modeoverride.h"

#include "compatibility.h"
#include "matching.h"
#include "settings.h"

#include "effect/effecthandler.h"
#include "wayland/clientconnection.h"
#include "wayland/display.h"
#include "wayland/output.h"
#include "wayland_server.h"

#include <QLoggingCategory>

#include <wayland-server-protocol.h>

#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

UpscaleModeOverride::UpscaleModeOverride()
{
    if (!available()) {
        return;
    }
    watchOutputs();
    // An output added later has its own resources, and a game started after
    // that binds them like any other. Effects hear about outputs before a
    // client can reach them, so this is early enough.
    connect(effects, &EffectsHandler::screenAdded, this, [this]() {
        watchOutputs();
    });
}

UpscaleModeOverride::~UpscaleModeOverride()
{
    restore();
}

bool UpscaleModeOverride::available()
{
    // An X11 session has no Wayland clients to address, and KWin can be built
    // and run without a Wayland server at all.
    return waylandServer() && waylandServer()->display();
}

void UpscaleModeOverride::reconfigure()
{
    // Every reconfiguration can change what would be asked for: the preset,
    // the percentage, or an edit to the application list, which this class
    // never sees. Rather than work out which of them moved, give back what was
    // taken. The games already running will not read it, but a client that
    // binds the output again, and the next thing that inspects these
    // resources, would otherwise see a mode this effect no longer asks for.
    if (!m_announced.isEmpty()) {
        restore(Record::Keep);
    }
}

QSize UpscaleModeOverride::advertised(const ClientConnection *client, const QString &output) const
{
    return m_advertised.value(client).value(output);
}

void UpscaleModeOverride::watchOutputs()
{
    const auto outputs = waylandServer()->display()->outputs();
    for (OutputInterface *output : outputs) {
        watchOutput(output);
    }
}

void UpscaleModeOverride::watchOutput(OutputInterface *output)
{
    // Every output change rescans, so remember which ones are already
    // watched: connecting twice would announce twice to the same client.
    // Qt's unique connections cannot do this, because they do not apply to
    // the lambda this needs.
    if (m_watched.contains(output)) {
        return;
    }
    m_watched.insert(output);
    const QString name = output->handle() ? output->handle()->name() : QString();
    connect(output, &QObject::destroyed, this, [this, output, name]() {
        m_watched.remove(output);
        for (auto &outputs : m_advertised) {
            outputs.remove(name);
        }
    });
    connect(output, &OutputInterface::bound, this,
            [this, output](ClientConnection *client, wl_resource *resource) {
        announce(output, client, resource);
    });
}

// What to tell one client: the buffer size it should end up committing, and
// the integer scale that goes with it. A zero scale means the size stands on
// its own as a mode; an empty size means there is nothing worth saying.
UpscaleModeOverride::Advertisement UpscaleModeOverride::advertisementFor(OutputInterface *output,
                                                                         const UpscaleSettings &settings,
                                                                         UpscaleMethod method)
{
    UpscaleOutput *handle = output->handle();
    if (!handle) {
        return {};
    }
    const QSize pixels = handle->pixelSize();
    if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, settings.value(UpscaleSetting::MinimumPixels))) {
        return {};
    }
    const ResolutionPreset preset = settings.resolution();
    const int percentage = settings.value(UpscaleSetting::Percentage);
    const UpscaleSize destination{pixels.width(), pixels.height()};
    if (method == UpscaleMethod::AdvertisedMode) {
        // This kind of client presents whatever size it picked through a
        // viewport that still covers the screen, so any calculated size is
        // reachable and the wish needs no adjusting.
        const UpscaleSize desired = desiredResolution(destination, preset, percentage);
        const QSize size(desired.width, desired.height);
        // Advertising the size the output already has says nothing, and the
        // scaler would have nothing to enlarge either.
        return size == pixels || size.isEmpty() ? Advertisement{} : Advertisement{size, 0};
    }
    // The other methods move a client only in whole steps of the output's own
    // scale, so the wish is answered with the nearest step that is reachable.
    const int scale = reachableScale(destination, handle->scale(), preset, percentage);
    if (scale <= 0) {
        return {};
    }
    const UpscaleSize reachable = scaledRequest(destination, handle->scale(), scale);
    return {QSize(reachable.width, reachable.height), scale};
}

// Whether a program is told a smaller screen at bind, and how: the profile
// that answers for it, its settings and the method. Nothing when it is not.
struct UpscaleBindDecision
{
    const UpscaleApplication *application = nullptr;
    UpscaleSettings settings;
    UpscaleMethod method = UpscaleMethod::Off;
};

static std::optional<UpscaleBindDecision> decideAtBind(ClientConnection *client)
{
    // KWin's own helper clients are never told anything. Xwayland above all:
    // it is one connection for every X11 program, so a smaller screen told to
    // it would be a smaller screen for all of them, and X11 games are asked
    // through their windows instead. The input method and the screen locker
    // are KWin's too, and neither is a game.
    if (client == waylandServer()->xWaylandConnection() || client == waylandServer()->inputMethodConnection()
        || client == waylandServer()->screenLockerClientConnection()) {
        return std::nullopt;
    }
    // The profile the program's path selects, or none. With none, the global
    // profile answers - which is what switching on unlisted applications and
    // giving the global profile a method is for - through the same code. A
    // path claimed by a profile that also names a window does not decide
    // yet, and nothing is advertised: that profile may still claim the
    // window, and an advertisement cannot be taken back.
    const UpscaleBindAnswer answer = upscaleApplicationAtBind(client->executablePath());
    if (!answer.decided) {
        return std::nullopt;
    }
    UpscaleBindDecision decision{
        .application = answer.application,
        .settings = upscaleResolveSettings(answer.application),
        .method = UpscaleMethod::Off,
    };
    if (!decision.settings.acts()) {
        return std::nullopt;
    }
    // What is said before the window exists comes from the fullscreen slot;
    // upscaleAdvertisedPresentation() carries why that one and not a coin
    // toss between the two Wayland answers.
    //
    // Auto tells the program a smaller screen mode here, whatever the program
    // is and whether an entry or the global profile answers for it: Auto means
    // the same wherever it comes from. A game in SDL's exclusive fullscreen
    // takes its buffer from the mode it is told now and from nothing said
    // later, so an Auto that waited for the window would leave it at full size
    // for the whole run. A program that sizes its buffer from the configure
    // instead ignores the mode, and is asked for a fractional scale once its
    // window exists (autorequest.cpp). Laid down by Jens on 2026-09-21. Only a
    // program the effect acts on hears it: one an entry claims, or any once
    // All applications is checked, which settings.acts() said above.
    decision.method = upscaleMethodFor(decision.application, upscaleAdvertisedPresentation());
    if (decision.method == UpscaleMethod::Auto) {
        decision.method = UpscaleMethod::AdvertisedMode;
    }
    if (decision.method == UpscaleMethod::Off || decision.method == UpscaleMethod::X11Resize) {
        return std::nullopt;
    }
    return decision;
}

// Sends the advertisement, or nothing when the client's output version
// cannot carry all of it.
static bool sendAdvertisement(UpscaleOutput *handle, wl_resource *resource, const QSize &size, int scale, UpscaleMethod method)
{
    const int version = wl_resource_get_version(resource);
    // A request that names a scale needs the event that carries one. Telling
    // such a client the smaller mode alone would leave its size and its scale
    // disagreeing, which is the state each of these methods exists to avoid,
    // and recording it would report a request that was never made.
    if (scale > 0 && version < WL_OUTPUT_SCALE_SINCE_VERSION) {
        return false;
    }
    if (method != UpscaleMethod::AdvertisedScale) {
        // The refresh rate stays the output's own. Only the size is in
        // question here, and a program that takes its frame pacing from this
        // should get the rate the screen actually runs at.
        wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, size.width(), size.height(),
                            int(handle->refreshRate()));
    }
    if (scale > 0) {
        wl_output_send_scale(resource, scale);
    }
    if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
        wl_output_send_done(resource);
    }
    return true;
}

void UpscaleModeOverride::announce(OutputInterface *output, ClientConnection *client, wl_resource *resource)
{
    if (!client || !resource) {
        return;
    }
    const std::optional<UpscaleBindDecision> decision = decideAtBind(client);
    if (!decision) {
        return;
    }
    const Advertisement advertisement = advertisementFor(output, decision->settings, decision->method);
    if (advertisement.size.isEmpty()
        || !sendAdvertisement(output->handle(), resource, advertisement.size, advertisement.scale, decision->method)) {
        return;
    }
    remember(output, client, advertisement.size);
    qCInfo(KWIN_UPSCALE) << "Wayland output advertised: pid" << client->processId()
                         << "program" << client->executablePath().section(QLatin1Char('/'), -1)
                         << "output" << output->handle()->name() << "native" << output->handle()->pixelSize()
                         << "advertised" << advertisement.size << "scale" << advertisement.scale
                         << "method" << upscaleMethodKey(decision->method);
}

void UpscaleModeOverride::remember(OutputInterface *output, ClientConnection *client, const QSize &size)
{
    // Recorded by the program's file name, whichever profile spoke, so that
    // restoring and reporting do not have to know which one it was.
    const QString program = client->executablePath().section(QLatin1Char('/'), -1);
    m_announced.append({output, client, program});
    if (!m_advertised.contains(client)) {
        connect(client, &QObject::destroyed, this, [this, client]() {
            m_advertised.remove(client);
        });
    }
    // A later instance of the same executable may have seen a different policy.
    m_advertised[client][output->handle()->name()] = size;
}

void UpscaleModeOverride::restore(Record record)
{
    for (const Announcement &announcement : std::as_const(m_announced)) {
        // A game that exited and an output that was unplugged both leave one
        // of these null. There is nothing to restore for either.
        if (!announcement.output || !announcement.client) {
            continue;
        }
        UpscaleOutput *handle = announcement.output->handle();
        if (!handle) {
            continue;
        }
        const QSize pixels = handle->pixelSize();
        const auto resources = announcement.output->clientResources(announcement.client->client());
        qCInfo(KWIN_UPSCALE) << "Wayland output restored: pid" << announcement.client->processId()
                             << "program" << announcement.program << "output" << handle->name() << "mode" << pixels
                             << "scale" << int(std::ceil(handle->scale())) << "resources" << resources.size();
        for (wl_resource *resource : resources) {
            wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                                pixels.width(), pixels.height(), int(handle->refreshRate()));
            if (wl_resource_get_version(resource) >= WL_OUTPUT_SCALE_SINCE_VERSION) {
                // Whatever KWin would have said, which for a fractional scale
                // is the integer at or above it rather than the scale itself.
                wl_output_send_scale(resource, int(std::ceil(handle->scale())));
            }
            if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION) {
                wl_output_send_done(resource);
            }
        }
    }
    m_announced.clear();
    if (record == Record::Discard) {
        // Keep the connection keys until destruction, so a later bind does not
        // install a second lifetime connection after every reconfiguration.
        for (auto &outputs : m_advertised) {
            outputs.clear();
        }
    }
}

} // namespace KWin
