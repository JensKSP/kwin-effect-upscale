/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "compatibility.h"
#include "eligibility.h"
#include "upscaleconfig.h"
#include "x11geometry.h"
#include "x11input.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "main.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "wayland_server.h"
#include "x11window.h"

#include <KLocalizedString>

#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QTimer>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)
#endif

namespace KWin
{

UpscaleX11Resolution::UpscaleX11Resolution()
#if KWIN_BUILD_X11
    : X11EventFilter(QList<int>{XCB_CLIENT_MESSAGE, XCB_CONFIGURE_REQUEST})
#endif
{
#if KWIN_BUILD_X11
    m_expiration.setSingleShot(true);
    m_expiration.setInterval(3000);
    connect(&m_expiration, &QTimer::timeout, this, &UpscaleX11Resolution::expireState);
    connect(effects, &EffectsHandler::windowAdded, this, &UpscaleX11Resolution::watch);
    connect(kwinApp(), &Application::x11ConnectionAboutToBeDestroyed, this, [this]() {
        ++m_generation;
        m_requests.clear();
        m_requested.clear();
        m_failures.clear();
        m_attempts.clear();
        m_retries.clear();
        m_validation.clear();
        m_waitingForBuffer.clear();
        m_stateAtom = XCB_ATOM_NONE;
        m_fullscreenAtom = XCB_ATOM_NONE;
    });
    connect(kwinApp(), &Application::xwaylandScaleChanged, this, [this]() {
        reconfigure(m_enabled, m_preset, m_percentage);
    });
    const auto watchOutput = [this](UpscaleOutput *output) {
        connect(output, &UpscaleOutput::geometryChanged, this, [this]() {
            reconfigure(m_enabled, m_preset, m_percentage);
        });
    };
    connect(effects, &EffectsHandler::screenAdded, this, watchOutput);
    connect(effects, &EffectsHandler::screenRemoved, this, [this]() {
        reconfigure(m_enabled, m_preset, m_percentage);
    });
    for (UpscaleOutput *output : effects->screens()) {
        watchOutput(output);
    }
    for (EffectWindow *window : effects->stackingOrder()) {
        watch(window);
    }
    m_input = std::make_unique<UpscaleX11Input>(this);
#endif
}

UpscaleX11Resolution::~UpscaleX11Resolution()
{
#if KWIN_BUILD_X11
    m_enabled = false;
    restoreAll();
#endif
}

void UpscaleX11Resolution::reconfigure(bool enabled, ResolutionPreset preset, int percentage)
{
#if KWIN_BUILD_X11
    ++m_generation;
    m_enabled = false;
    restoreAll();
    m_requested.clear();
    m_failures.clear();
    m_attempts.clear();
    m_retries.clear();
    m_validation.clear();
    m_waitingForBuffer.clear();
    m_preset = preset;
    m_percentage = percentage;
    m_enabled = enabled && waylandServer();
    for (X11Window *window : std::as_const(m_watched)) {
        schedule(window);
    }
#else
    Q_UNUSED(enabled)
    Q_UNUSED(preset)
    Q_UNUSED(percentage)
#endif
}

QSize UpscaleX11Resolution::requested(const Window *window) const
{
#if KWIN_BUILD_X11
    return m_requested.value(keyFor(window));
#else
    Q_UNUSED(window)
    return {};
#endif
}

QString UpscaleX11Resolution::failure(const Window *window) const
{
#if KWIN_BUILD_X11
    return m_failures.value(keyFor(window));
#else
    Q_UNUSED(window)
    return {};
#endif
}

#if KWIN_BUILD_X11
QString UpscaleX11Resolution::keyFor(const Window *window)
{
    if (!window || !window->output()) {
        return {};
    }
    const UpscaleApplication *application = upscaleApplicationForIdentity(window->resourceClass(), window->resourceName());
    // SFML replaces XIDs while retaining its process. Keep negotiation across
    // those replacements. PID is only a grouping hint, not a launch identity:
    // expire orphaned state after a replacement grace period. Use KWin's
    // identity, not /proc.
    return application && application->method == UpscaleControlMethod::X11Resize
        ? application->id + QLatin1Char('/') + window->output()->name() + QLatin1Char('/') + QString::number(window->pid())
        : QString();
}

void UpscaleX11Resolution::watch(EffectWindow *effectWindow)
{
    auto window = qobject_cast<X11Window *>(effectWindow->window());
    if (!window || window->isUnmanaged() || m_watched.contains(window)) {
        return;
    }
    m_watched.insert(window);
    connect(window, &Window::closed, this, [this, window]() {
        forget(window);
    });
    connect(window, &QObject::destroyed, this, [this, window]() {
        forget(window);
    });
    connect(window, &Window::windowClassChanged, this, [this, window]() {
        schedule(window);
    });
    connect(window, &Window::fullScreenChanged, this, [this, window]() {
        schedule(window);
    });
    connect(window, &Window::frameGeometryChanged, this, [this, window]() {
        schedule(window);
    });
    connect(effectWindow, &EffectWindow::windowDamaged, this, [this, window]() {
        if (m_waitingForBuffer.remove(window)) {
            schedule(window);
        }
        present(window);
    });
    schedule(window);
}

void UpscaleX11Resolution::schedule(X11Window *window)
{
    if (m_restoring || m_scheduled.contains(window)) {
        return;
    }
    m_scheduled.insert(window);
    const QPointer<X11Window> guarded = window;
    QTimer::singleShot(0, this, [this, guarded]() {
        if (guarded) {
            m_scheduled.remove(guarded);
            apply(guarded);
        }
    });
}

UpscaleX11Resolution::Request UpscaleX11Resolution::requestFor(X11Window *window) const
{
    if (!m_enabled || m_restoring || !kwinApp()->x11Connection() || window->isDeleted()
        || window->isUnmanaged() || !window->isNormalWindow() || !window->output()
        || window->output()->transform() != OutputTransform::Normal) {
        return {};
    }
    const QString key = keyFor(window);
    if (key.isEmpty() || m_failures.contains(key)) {
        return {};
    }
    const UpscaleApplication *application = upscaleApplicationForIdentity(window->resourceClass(), window->resourceName());
    const ResolutionPreset preset = effectiveResolutionPreset(m_preset, application->preset);
    const QSize pixels = window->output()->pixelSize();
    const int minimum = application->minimumPixels < 0 ? UpscaleConfig::minimumPixels() : application->minimumPixels;
    if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, minimum)) {
        return {};
    }
    const UpscaleSize size = desiredResolution({pixels.width(), pixels.height()}, preset, m_percentage);
    if (!canUpscale(size, {pixels.width(), pixels.height()}) || size.width > 65535 || size.height > 65535) {
        return {};
    }
    // Output ownership comes from this window, never from the active screen.
    const qreal scale = kwinApp()->xwaylandScale();
    const QPoint position(qRound(window->output()->geometryF().x() * scale),
                          qRound(window->output()->geometryF().y() * scale));
    return {window, key, position, QSize(size.width, size.height), application->x11PrimaryOutputOnly};
}

bool UpscaleX11Resolution::begin(const Request &request)
{
    if (!request.window) {
        return false;
    }
    watch(request.window->effectWindow());
    const auto previous = m_requests.constFind(request.window);
    if (previous != m_requests.cend() && previous->key == request.key
        && previous->position == request.position && previous->size == request.size) {
        return true;
    }
    // A move must release the old output's geometry before validating the new
    // request. Otherwise a refused destination would leave its ConfigureRequest
    // events intercepted with the previous output's position.
    restore(request.window);
    if (request.primaryOnly && !upscaleX11PrimaryOutput(request.position)) {
        refuse(request.key, i18n("This application's X11 mode selection only supports the primary output."));
        return false;
    }
    if (!upscaleX11ModeAvailable(request.position, request.size)) {
        refuse(request.key, i18n("The requested X11 mode is unavailable on this output."));
        return false;
    }
    // Bound recreation loops across successive XIDs. Refusal lasts until the
    // next reconfiguration or the end of its window lifecycle, so restoring a
    // client cannot immediately retry it.
    // Leaving and re-entering fullscreen on the same XID is not replacement.
    Attempt &attempt = m_attempts[request.key];
    if (attempt.window != request.window) {
        attempt.window = request.window;
        ++attempt.count;
    }
    if (attempt.count > 6) {
        refuse(request.key, i18n("The application repeatedly replaced its window without accepting the request."));
        return false;
    }
    m_requests.insert(request.window, request);
    m_requested.insert(request.key, request.size);
    const int generation = m_generation;
    const int revision = ++m_nextValidation;
    m_validation.insert(request.key, revision);
    QTimer::singleShot(3000, this, [this, key = request.key, generation, revision]() {
        validate(key, generation, revision);
    });
    return true;
}

void UpscaleX11Resolution::apply(X11Window *window)
{
    if (window->isDeleted()) {
        return;
    }
    // Fullscreen is a state, not a size, and upscalePresentation() answers the
    // state. A client holds it while its window is still being sized during
    // startup - measured on Left 4 Dead 2, 2026-09-19: four resizes between
    // the output size and its own in the first 1.5 seconds, every one of them
    // fullscreen - and a window this effect has itself made smaller holds it
    // as well. Beginning a negotiation there resizes a window that was never
    // presenting full-screen, which is how this effect shrinks a game instead
    // of scaling it. A request already in flight is deliberately exempt: its
    // window is legitimately smaller natively, and validate() owns the
    // question of whether that request held.
    const bool negotiating = m_requests.contains(window);
    const bool presenting = upscalePresentation(window->effectWindow())
        && (negotiating || upscaleCoversOutput(window->effectWindow()));
    const Request request = presenting ? requestFor(window) : Request{};
    if (!request.window) {
        restore(window);
        return;
    }
    // A configure sent during window construction can be consumed before the
    // game initializes its renderer. Start negotiation after its first buffer.
    // Replacement windows with an existing emulated mode are handled earlier
    // by the fullscreen event filter, so they cannot oscillate back to native.
    SurfaceItem *surface = window->effectWindow()->windowItem()->surfaceItem();
    if (!surface || surface->bufferSize().isEmpty()) {
        m_waitingForBuffer.insert(window);
        return;
    }
    const auto previous = m_requests.constFind(window);
    if (previous != m_requests.cend() && previous->key == request.key
        && previous->position == request.position && previous->size == request.size) {
        return;
    }
    if (begin(request)) {
        upscaleX11Configure(window, request.position, request.size);
        qCDebug(KWIN_UPSCALE) << "Requested X11 buffer" << request.size << "from" << request.key;
    }
}

bool UpscaleX11Resolution::retry(const QString &key, int generation)
{
    if (m_retries.value(key) != 0) {
        return false;
    }
    // Clients can discard resize events during a loading/state transition.
    // One retry returns to normal geometry first: duplicate ConfigureNotify
    // events may be ignored if the toolkit cached the requested size already.
    // Never loop on a client which cannot establish full-output presentation.
    m_retries.insert(key, 1);
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        if (m_requests.value(window).key != key) {
            continue;
        }
        const QPointer<X11Window> guarded = window;
        restore(window);
        QTimer::singleShot(250, this, [this, guarded, generation]() {
            if (guarded && generation == m_generation) {
                schedule(guarded);
            }
        });
    }
    return true;
}

void UpscaleX11Resolution::refuse(const QString &key, const QString &reason)
{
    m_failures.insert(key, reason);
    m_requested.remove(key);
    qCWarning(KWIN_UPSCALE) << "X11 resolution control:" << key << reason;
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        if (m_requests.value(window).key == key) {
            restore(window);
        }
    }
}

void UpscaleX11Resolution::restore(X11Window *window)
{
    m_waitingForBuffer.remove(window);
    const Request request = m_requests.take(window);
    if (!request.window) {
        return;
    }
    // Status describes current control, not a request that has been released.
    if (std::ranges::none_of(m_requests, [&request](const Request &other) {
        return other.key == request.key;
    })) {
        m_requested.remove(request.key);
    }
    // The pointer gets KWin's own mapping back now, not at its next move.
    m_input->refresh();
    if (window->isDeleted() || !kwinApp()->x11Connection()) {
        return;
    }
    const QScopedValueRollback restoring(m_restoring, true);
    // KWin's logical geometry remained authoritative throughout. Hand its
    // normal native size back before ceasing geometry interception. This also
    // brings the X server back into agreement with KWin's geometry caches.
    upscaleX11Configure(window, upscaleX11Position(window), upscaleX11NormalSize(window));
}

void UpscaleX11Resolution::restoreAll()
{
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        restore(window);
    }
}
#endif

} // namespace KWin
