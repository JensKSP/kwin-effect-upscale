/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "preparation.h"
#include "application.h"
#include "runtime.h"
#include "settings.h"
#include "windowidentity.h"
#include "x11resolution.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "window.h"

#include <KLocalizedString>
#include <QLoggingCategory>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(KWIN_UPSCALE)

namespace KWin
{

// The name this effect is known by, as its metadata gives it: a question in
// the middle of a game says who is asking.
static QString upscaleName()
{
    return i18nc("@title the name of this effect, above a question it asks", "Upscale");
}

static const QString acceptAnswer = QStringLiteral("accept");
static const QString laterAnswer = QStringLiteral("later");
static const QString neverAnswer = QStringLiteral("never");
static const QString restartAnswer = QStringLiteral("restart");

UpscalePreparation::UpscalePreparation(Effect *owner, UpscaleX11Resolution *x11)
    : m_owner(owner)
    , m_x11(x11)
{
    // A question on an output that goes away is taken away with it, keyboard
    // and pointer included.
    connect(effects, &EffectsHandler::screenRemoved, this, [this](UpscaleOutput *output) {
        m_question.outputRemoved(output);
    });
}

UpscaleQuestion &UpscalePreparation::question()
{
    return m_question;
}

void UpscalePreparation::unfollowed(EffectWindow *window, const QSize &size)
{
    offerSetup(window, size, true);
}

void UpscalePreparation::offerSetup(EffectWindow *window, const QSize &size, bool afterFailure)
{
    m_asked.removeAll(nullptr);
    if (m_asked.contains(window)) {
        return;
    }
    m_asked.append(window);
    const QPointer<EffectWindow> guarded = window;
    m_helper.offer(window, size, [this, guarded](const QString &offer, const QString &question) {
        if (guarded && !offer.isEmpty()) {
            askToSetUp(guarded, offer, question);
        }
    }, afterFailure);
}

void UpscalePreparation::askToSetUp(const QPointer<EffectWindow> &window, const QString &offer, const QString &question)
{
    const QList<UpscaleQuestion::Answer> answers{
        {acceptAnswer, i18nc("@action:button in the question in the middle of the screen", "Set up")},
        {laterAnswer, i18nc("@action:button in the question in the middle of the screen", "Not now")},
        {neverAnswer, i18nc("@action:button in the question in the middle of the screen", "Never for this game")},
    };
    const UpscaleQuestion::Content content{
        .title = upscaleName(),
        .text = question,
        .answers = answers,
        .cancel = laterAnswer,
    };
    const bool shown = m_question.ask(m_owner, window->screen(), content, [this, window, offer](const QString &answer) {
        m_helper.answer(offer, answer, [this, window, offer](const QString &restart) {
            if (window && !restart.isEmpty()) {
                askToRestart(window, offer, restart);
            }
        });
    });
    // Another question was open, or another effect holds the keyboard. Nothing
    // was asked, so the window may be asked about the next time.
    if (!shown) {
        m_asked.removeAll(window);
    }
}

void UpscalePreparation::askToRestart(const QPointer<EffectWindow> &window, const QString &offer, const QString &question)
{
    const QList<UpscaleQuestion::Answer> answers{
        {restartAnswer, i18nc("@action:button in the question in the middle of the screen", "Restart game and apply")},
        {laterAnswer, i18nc("@action:button in the question in the middle of the screen", "Later")},
    };
    const UpscaleQuestion::Content content{
        .title = upscaleName(),
        .text = question,
        .answers = answers,
        .cancel = laterAnswer,
    };
    m_question.ask(m_owner, window->screen(), content, [this, window, offer](const QString &answer) {
        if (answer != restartAnswer) {
            return;
        }
        // The window is asked to close, the way its own close button would;
        // the helper waits for the program to exit and starts it again.
        if (window) {
            window->closeWindow();
        }
        m_helper.restart(offer);
    });
}

void UpscalePreparation::windowAdded(EffectWindow *window)
{
#if KWIN_BUILD_X11
    // Every ordinary X11 window is asked about, not only those of programs the
    // effect acts on: a program the effect has stopped acting on may still be
    // prepared, and the helper undoes that only when it is told so.
    if (!window->isX11Client() || !window->isNormalWindow()
        || (window->isFullScreen() && !upscaleWineRuntime(upscaleExecutableOf(window->window())))) {
        return;
    }
    const QPointer<EffectWindow> guarded = window;
    // Not before the window has an output. What the effect wants is decided
    // per output, and "nothing" is what tells the helper to undo a
    // preparation, so it may not be said while nothing is known yet.
    if (!window->window()->output()) {
        QTimer::singleShot(0, this, [this, guarded]() {
            if (guarded && guarded->window()->output()) {
                ask(guarded);
            }
        });
        return;
    }
    ask(window);
#else
    Q_UNUSED(window)
#endif
}

#if KWIN_BUILD_X11
static QSize preparationSize(const Window *window)
{
    // Off in the fullscreen slot, where such a game presents itself, asks the
    // program for nothing, and so wants nothing prepared either.
    if (upscaleMethodFor(upscaleApplicationForWindow(window), UpscalePresentation::X11FullScreen) == UpscaleMethod::Off) {
        return {};
    }
    return upscaleWantedSize(window);
}

void UpscalePreparation::ask(EffectWindow *window)
{
    const Window *internal = window->window();
    const QSize wanted = preparationSize(internal);
    const QPointer<EffectWindow> guarded = window;
    const bool preparationOnly = upscaleWineRuntime(upscaleExecutableOf(internal));
    if (preparationOnly && !wanted.isEmpty()) {
        qCInfo(KWIN_UPSCALE) << "Wine runtime: leaving running geometry unchanged while checking preparation; pid" << window->pid()
                             << "wanted" << wanted;
    }
    m_helper.present(window, wanted, [this, guarded, wanted, preparationOnly](const QSize &size) {
        if (!guarded) {
            return;
        }
        if (preparationSize(guarded->window()) != wanted) {
            // Settings can change while the helper is answering. Supersede
            // that answer, including any queued preparation, before acting.
            ask(guarded);
            return;
        }
        if (!wanted.isEmpty() && !size.isEmpty()) {
            m_x11->presentPrepared(guarded, size);
        } else if (preparationOnly && !wanted.isEmpty()) {
            // No resize experiment precedes this offer. Acceptance prepares a
            // later launch; only a proven smaller buffer can be presented now.
            offerSetup(guarded, wanted, false);
        }
    });
}
#endif

} // namespace KWin
