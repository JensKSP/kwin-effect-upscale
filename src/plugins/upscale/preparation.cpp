/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "preparation.h"
#include "settings.h"
#include "windowidentity.h"
#include "x11resolution.h"

#include "effect/effectwindow.h"

#include <KLocalizedString>

namespace KWin
{

static const QString s_accept = QStringLiteral("accept");
static const QString s_later = QStringLiteral("later");
static const QString s_never = QStringLiteral("never");
static const QString s_restart = QStringLiteral("restart");

UpscalePreparation::UpscalePreparation(Effect *owner, UpscaleX11Resolution *x11)
    : m_owner(owner)
    , m_x11(x11)
{
}

UpscaleQuestion &UpscalePreparation::question()
{
    return m_question;
}

void UpscalePreparation::unfollowed(EffectWindow *window, const QSize &size)
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
    });
}

void UpscalePreparation::askToSetUp(const QPointer<EffectWindow> &window, const QString &offer, const QString &question)
{
    const QList<UpscaleQuestion::Answer> answers{
        {s_accept, i18nc("@action:button in the question in the middle of the screen", "Set up")},
        {s_later, i18nc("@action:button in the question in the middle of the screen", "Not now")},
        {s_never, i18nc("@action:button in the question in the middle of the screen", "Never for this game")},
    };
    m_question.ask(m_owner, window->screen(), question, answers, s_later, [this, window, offer](const QString &answer) {
        m_helper.answer(offer, answer, [this, window, offer](const QString &restart) {
            if (window && !restart.isEmpty()) {
                askToRestart(window, offer, restart);
            }
        });
    });
}

void UpscalePreparation::askToRestart(const QPointer<EffectWindow> &window, const QString &offer, const QString &question)
{
    const QList<UpscaleQuestion::Answer> answers{
        {s_restart, i18nc("@action:button in the question in the middle of the screen", "Restart game and apply")},
        {s_later, i18nc("@action:button in the question in the middle of the screen", "Later")},
    };
    m_question.ask(m_owner, window->screen(), question, answers, s_later, [this, window, offer](const QString &answer) {
        if (answer != s_restart) {
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
    // Only an X11 window the effect acts on can be one to present: a program
    // the effect leaves alone is left alone, prepared or not.
    if (!window->isX11Client() || !window->isNormalWindow() || window->isFullScreen()
        || !upscaleResolveSettings(upscaleApplicationForWindow(window->window())).acts()) {
        return;
    }
    const QPointer<EffectWindow> guarded = window;
    m_helper.present(window, [this, guarded](const QSize &size) {
        if (guarded && !size.isEmpty()) {
            m_x11->presentPrepared(guarded, size);
        }
    });
}

} // namespace KWin
