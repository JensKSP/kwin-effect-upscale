/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11resolution.h"

#if KWIN_BUILD_X11
#include "compatibility.h"
#include "eligibility.h"
#include "upscaleconfig.h"
#include "windowidentity.h"
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
#include <optional>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)
#endif

namespace KWin
{

UpscaleX11Resolution::UpscaleX11Resolution()
#if KWIN_BUILD_X11
    : X11EventFilter(QList<int>{XCB_CLIENT_MESSAGE, XCB_CONFIGURE_REQUEST, XCB_PROPERTY_NOTIFY})
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
        m_withdrawals.clear();
        m_overdue.clear();
        m_releases.clear();
        m_stateAtom = XCB_ATOM_NONE;
        m_fullscreenAtom = XCB_ATOM_NONE;
        m_emulationAtom = XCB_ATOM_NONE;
    });
    connect(kwinApp(), &Application::xwaylandScaleChanged, this, [this]() {
        reconfigure();
    });
    const auto watchOutput = [this](UpscaleOutput *output) {
        connect(output, &UpscaleOutput::geometryChanged, this, [this]() {
            reconfigure();
        });
    };
    connect(effects, &EffectsHandler::screenAdded, this, watchOutput);
    connect(effects, &EffectsHandler::screenRemoved, this, [this]() {
        reconfigure();
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

void UpscaleX11Resolution::reconfigure()
{
#if KWIN_BUILD_X11
    ++m_generation;
    m_enabled = false;
    // Released rather than restored outright: a request the client has not
    // answered yet is given back only once it has, so that its answer cannot
    // arrive after the window was handed back; see release().
    const auto windows = m_requests.keys();
    for (X11Window *window : windows) {
        release(window);
    }
    m_requested.clear();
    m_failures.clear();
    m_attempts.clear();
    m_retries.clear();
    m_validation.clear();
    m_waitingForBuffer.clear();
    m_overdue.clear();
    // m_withdrawals and m_releases stay: they record what the clients are
    // doing, which a change of configuration does not alter, and the releases
    // above have just added to them.
    //
    // Nothing global is held any more: what to ask of a window comes from the
    // profile that claims it, resolved when the window is looked at. All this
    // still needs to know is whether there is an Xwayland to talk to.
    m_enabled = waylandServer();
    for (X11Window *window : std::as_const(m_watched)) {
        schedule(window);
    }
#endif
}

bool UpscaleX11Resolution::settled() const
{
#if KWIN_BUILD_X11
    return !m_restoring && m_scheduled.isEmpty() && m_releases.isEmpty() && m_withdrawals.isEmpty()
        && m_waitingForBuffer.isEmpty();
#else
    return true;
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

// Which of the three X11 cells this window is in. Unlike the Wayland half,
// this can be answered honestly: the window is already here, so its state and
// its geometry are both readable. A window the effect itself made smaller
// still holds the fullscreen state, which is why the state is asked first and
// the geometry only decides between the other two.
static UpscalePresentation x11PresentationOf(const Window *window)
{
    // In whole pixels, as the scaler judges coverage: an exact comparison of
    // logical rectangles refuses a window a rounding off the output's edge.
    const EffectWindow *effectWindow = window->effectWindow();
    const bool coversOutput = window->output() && effectWindow && upscaleCoversOutput(effectWindow);
    return upscalePresentationFor(true, window->isFullScreen(), !window->isDecorated() && coversOutput);
}

// Whether this window is one to ask for a smaller drawable.
//
// Auto and an explicit X11Resize both resize, and on X11 they mean the same
// thing for a good reason: there is one method, the window exists before
// anything is asked of it, and a request that loses coverage is observed and
// put back by the validation below. Auto has nothing to choose between and
// nothing it cannot undo, which is exactly what it does not have on Wayland.
//
// A windowed presentation is never resized. Shrinking a window the user sized
// only makes it smaller: the destination is the window's own size, so there is
// no gap left to enlarge into, and holding the frame while the client renders
// below it is not something any implemented path does.
//
// A window no entry claims is the global profile's, which asks it as well once
// All applications is checked; @p application is null then.
static bool upscaleX11ResizeWanted(const UpscaleApplication *application, const Window *window)
{
    const UpscalePresentation presentation = x11PresentationOf(window);
    if (upscaleIsWindowed(presentation)) {
        return false;
    }
    const UpscaleSettings settings = upscaleResolveSettings(application);
    if (!settings.acts() || settings.resolution() == ResolutionPreset::Native) {
        return false;
    }
    const UpscaleMethod method = upscaleMethodFor(application, presentation);
    return method == UpscaleMethod::Auto || method == UpscaleMethod::X11Resize;
}

#if KWIN_BUILD_X11
QString UpscaleX11Resolution::keyFor(const Window *window)
{
    if (!window || !window->output()) {
        return {};
    }
    const UpscaleApplication *application = upscaleApplicationForWindow(window);
    // SFML replaces XIDs while retaining its process. Keep negotiation across
    // those replacements. PID is only a grouping hint, not a launch identity:
    // expire orphaned state after a replacement grace period. Use KWin's
    // identity, not /proc.
    // A window the global profile answers for is keyed by an empty entry
    // name, which no entry has: a name that reduces to nothing is stored as
    // "application".
    if (!upscaleX11ResizeWanted(application, window)) {
        return QString();
    }
    return (application ? application->id : QString()) + QLatin1Char('/') + window->output()->name() + QLatin1Char('/')
        + QString::number(window->pid());
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
    const UpscaleApplication *application = upscaleApplicationForWindow(window);
    const UpscaleSettings settings = upscaleResolveSettings(application);
    const QSize pixels = window->output()->pixelSize();
    if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, settings.value(UpscaleSetting::MinimumPixels))) {
        return {};
    }
    const UpscaleSize size = desiredResolution({pixels.width(), pixels.height()}, settings.resolution(),
                                               settings.value(UpscaleSetting::Percentage));
    if (!canUpscale(size, {pixels.width(), pixels.height()}) || size.width > 65535 || size.height > 65535) {
        return {};
    }
    // Output ownership comes from this window, never from the active screen.
    const qreal scale = kwinApp()->xwaylandScale();
    const QPoint position(qRound(window->output()->geometryF().x() * scale),
                          qRound(window->output()->geometryF().y() * scale));
    // Every field named, the last three with how a request starts out: not
    // presented by the effect, not answered by the client, and no verdict
    // until begin() sets one. Naming them keeps -Wmissing-field-initializers
    // satisfied without an initializer on the timer that says nothing.
    // The global profile has no primary-output rule: that is measured per game.
    const bool primaryOnly = application && application->x11PrimaryOutputOnly;
    return {window, key, position, QSize(size.width, size.height), primaryOnly, false, false, {}};
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
    Request live = request;
    // A client may already hold the mode this asks for, as a replacement
    // window does; then there is no answer left to wait for.
    live.answered = upscaleX11ModeMatches(request.window, request.position, request.size);
    live.verdict.setRemainingTime(s_validationWindow);
    m_requests.insert(request.window, live);
    m_requested.insert(request.key, request.size);
    const int generation = m_generation;
    const int revision = ++m_nextValidation;
    m_validation.insert(request.key, revision);
    QTimer::singleShot(s_validationWindow, this, [this, key = request.key, generation, revision]() {
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
    if (!presenting) {
        // The window's own state changed under the request - it left
        // fullscreen, or stopped covering its output - so the client is
        // resizing it itself, and the request is given back at once.
        restore(window);
        return;
    }
    // A request being released is not touched until its client has answered
    // it; the release looks at the window again when that is done.
    if (m_releases.contains(window)) {
        return;
    }
    const Request request = requestFor(window);
    if (!request.window) {
        // Nothing is asked of a window that still presents: the configuration
        // or the profile that claimed it changed. Its request is released
        // the way a reconfiguration releases them, once the client has
        // answered it, so that the answer cannot overtake the restore.
        release(window);
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
    if (previous != m_requests.cend()) {
        if (previous->key == request.key && previous->position == request.position && previous->size == request.size) {
            return;
        }
        // Released here rather than in begin(), so that the waits release()
        // and restore() start are respected below: the client answers the
        // release by withdrawing its mode, and that answer has to arrive
        // before anything else is asked of it.
        release(window);
        if (m_releases.contains(window)) {
            return;
        }
    }
    ask(window, request);
}

// Nothing is asked of the client until it has withdrawn the emulated mode of
// the request before this one. KWin 6.6 sizes the client window from the
// _XWAYLAND_RANDR_EMU_MONITOR_RECTS property whenever Xwayland changes it: to
// the mode it names, or to the full frame once it has gone. A client
// withdraws its mode in answer to a restore, and a request made before that
// answer reaches KWin is applied first and undone by it. Measured on KWin
// 6.6.6 with Xwayland 24.1 on 2026-09-21, polling the window every 10 ms
// after such a request: 3840 x 2160, 1920 x 1080 at 17 ms, 3840 x 2160 at
// 35 ms, and 1920 x 1080 again only at 3117 ms, when validation had failed
// and its retry - restore, 250 ms, request again - had put it right. Most
// preset changes and re-enables on 6.6 cost that retry, and the integration
// test raced an 18 ms transient. restore() starts the wait and
// emulatedModeChanged() ends it. A mode the client holds that is neither the
// request nor being withdrawn, one it chose itself, is waited for the same
// way; once that wait has run out the request is made regardless, and
// validation says what became of it. KWin 6.3 never reads the property; there
// the wait costs only the milliseconds the withdrawal takes.
void UpscaleX11Resolution::ask(X11Window *window, const Request &request)
{
    if (m_withdrawals.contains(window)) {
        return;
    }
    const std::optional<QSize> held = upscaleX11EmulatedMode(window, request.position);
    if (held && *held != request.size && !m_overdue.contains(window)) {
        awaitWithdrawal(window);
        return;
    }
    m_overdue.remove(window);
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

// Gives the window back at once, whether or not its client has answered the
// request. release() is the way to give one back that respects the answer;
// this is for the cases that cannot wait for it: the client left fullscreen
// or stopped presenting, validation has judged the request, or this object
// is going away with the effect.
void UpscaleX11Resolution::restore(X11Window *window)
{
    m_waitingForBuffer.remove(window);
    m_releases.remove(window);
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
    // present() sized the surface item to the frame so that KWin would paint
    // the whole enlarged image rather than its top-left corner. A client that
    // resizes to its normal geometry ends that by itself, because a buffer of
    // another size makes KWin recompute the destination from the surface. A
    // client that goes on committing the same buffer never does, and would
    // stay stretched after this effect stopped presenting it - which is the
    // case on an unload, where the request is withdrawn without the client
    // having been asked for anything. Hand KWin's own value back instead of
    // waiting for a size change that may never come.
    if (request.presentedByEffect && !window->isDeleted()) {
        WindowItem *item = window->effectWindow() ? window->effectWindow()->windowItem() : nullptr;
        if (SurfaceItem *surface = item ? item->surfaceItem() : nullptr) {
            surface->setDestinationSize(window->bufferGeometry().size());
        }
    }
    // The pointer gets KWin's own mapping back now, not at its next move.
    m_input->refresh();
    if (window->isDeleted() || !kwinApp()->x11Connection()) {
        return;
    }
    // Read before the window is handed back, so that what is read is the
    // client's state from before it could have answered.
    const bool held = upscaleX11EmulatedMode(window, request.position).has_value();
    const QScopedValueRollback restoring(m_restoring, true);
    // KWin's logical geometry remained authoritative throughout. Hand its
    // normal native size back before ceasing geometry interception. This also
    // brings the X server back into agreement with KWin's geometry caches.
    upscaleX11Configure(window, upscaleX11Position(window), upscaleX11NormalSize(window));
    if (held) {
        awaitWithdrawal(window);
    }
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
