/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "helper.h"
#include "question.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>

namespace KWin
{

class EffectWindow;
class UpscaleX11Resolution;

/**
 * What the effect does about a program that does not render at the size
 * wanted and cannot be made to while it runs.
 *
 * It asks the optional helper (UpscaleHelper) whether the program can be
 * prepared to render at that size from its next start, puts the helper's
 * question to the user in the middle of the screen, and after a yes offers to
 * restart the program. For each new window of a program the helper prepared,
 * it has the X11 control present the window across its output.
 *
 * Nothing here knows what a helper does or how. Without one, nothing is asked
 * and nothing changes. Recognized Wine runtimes skip resize experiments and
 * ask about preparation directly; a helper answer still needs an actual
 * smaller buffer before presentation changes.
 */
class UpscalePreparation : public QObject
{
    Q_OBJECT

public:
    UpscalePreparation(Effect *owner, UpscaleX11Resolution *x11);

    /** Validation found that @p window draws at another size than @p size. */
    void unfollowed(EffectWindow *window, const QSize &size);
    /** A new window, which may be one a helper prepared its program for. */
    void windowAdded(EffectWindow *window);
    UpscaleQuestion &question();

private:
    void offerSetup(EffectWindow *window, const QSize &size, bool afterFailure);
    void ask(EffectWindow *window);
    void askToSetUp(const QPointer<EffectWindow> &window, const QString &offer, const QString &question);
    void askToRestart(const QPointer<EffectWindow> &window, const QString &offer, const QString &question);

    Effect *m_owner;
    UpscaleX11Resolution *m_x11;
    UpscaleHelper m_helper;
    UpscaleQuestion m_question;
    // Windows already asked about, so that a retry never asks twice.
    QList<QPointer<EffectWindow>> m_asked;
};

} // namespace KWin
