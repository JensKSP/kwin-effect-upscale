/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "waylandscale.h"

#include "application.h"
#include "resolution.h"
#include "settings.h"
#include "windowidentity.h"

#include "wayland/clientconnection.h"
#include "wayland/surface.h"
#include "wayland/xdgshell.h"
#include "wayland_server.h"
#include "window.h"

#include <wayland-server-core.h>

#include <cmath>

namespace KWin
{

// A client with no fractional-scale object cannot express a buffer scale
// below one. Ask its initial fullscreen size instead: glmark2 resizes its EGL
// window on later configures but keeps the viewport established at startup.
// This is a connection-wide conservative check; another surface using the
// protocol leaves this client on the existing live scale path.
static bool hasFractionalScale(ClientConnection *client)
{
    bool found = false;
    wl_client_for_each_resource(client->client(), [](wl_resource *resource, void *data) {
        if (QByteArrayView(wl_resource_get_class(resource)) == QByteArrayView("wp_fractional_scale_v1")) {
            *static_cast<bool *>(data) = true;
            return WL_ITERATOR_STOP;
        }
        return WL_ITERATOR_CONTINUE;
    }, &found);
    return found;
}

void UpscaleWaylandScale::watchInitialSizes()
{
    if (!waylandServer()) {
        return;
    }
    auto *shell = waylandServer()->findChild<XdgShellInterface *>();
    if (!shell) {
        return;
    }
    connect(shell, &XdgShellInterface::toplevelCreated, this, [this](XdgToplevelInterface *toplevel) {
        // KWin connected its initializer before the effect loaded. It has
        // chosen the fullscreen output when this runs, and its configure
        // timer has not yet sent the first size. Find the current window each
        // time: unmapping and mapping a toplevel recreates KWin's window.
        connect(toplevel, &XdgToplevelInterface::initializeRequested, this, [this, toplevel]() {
            requestInitialSize(waylandServer()->findWindow(toplevel->surface()));
        });
    });
}

void UpscaleWaylandScale::requestInitialSize(Window *window)
{
    if (!window || !window->isNormalWindow() || !window->isRequestedFullScreen() || !window->output()
        || !window->surface() || window->surface()->buffer() || window->readyForPainting() || known(window)) {
        return;
    }
    ClientConnection *client = window->surface()->client();
    if (client == waylandServer()->xWaylandConnection() || client == waylandServer()->inputMethodConnection()
        || client == waylandServer()->screenLockerClientConnection() || hasFractionalScale(client)) {
        return;
    }
    // Keep the established high-density route. This path addresses the lower
    // bound of the integer protocol, rather than changing its rounding policy.
    if (std::abs(window->output()->scale() - 1.0) > 0.001 || window->output()->transform() != OutputTransform::Normal) {
        return;
    }
    const UpscaleApplication *claimed = upscaleApplicationForWindow(window);
    const UpscaleSettings settings = upscaleSettingsForWindow(window);
    const QSize pixels = window->output()->pixelSize();
    const double ratio = resolutionRatio(settings.resolution(), settings.value(UpscaleSetting::Percentage));
    if (!settings.acts() || upscaleMethodFor(claimed, UpscalePresentation::WaylandFullScreen) != UpscaleMethod::Auto
        || ratio < 0.5 || ratio >= 1.0
        || !exceedsMinimumPixels({pixels.width(), pixels.height()}, settings.value(UpscaleSetting::MinimumPixels))) {
        return;
    }
    Request fresh;
    fresh.original = window->nextTargetScale();
    fresh.ratio = ratio;
    auto entry = m_requests.insert(window, fresh);
    observe(window);
    resize(window, *entry);
}

} // namespace KWin
