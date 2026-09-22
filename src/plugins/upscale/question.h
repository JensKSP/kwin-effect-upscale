/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"
#include "overlay.h"

#include <QList>
#include <QPointer>
#include <QRectF>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QKeyEvent;

namespace KWin
{

/**
 * A question in the middle of an output, with the answers beside each other
 * underneath it: the on-screen display that asks rather than reports.
 *
 * It is drawn like the passive blocks, after the game pass and outside the
 * captured image, but it takes the keyboard until it is answered, because a
 * question the game keeps its keys from cannot be answered. Left, right and
 * Tab move between the answers, Return chooses one and Escape chooses the one
 * that postpones. It holds the pointer as well: hovering an answer selects it
 * and a click chooses it.
 */
class UpscaleQuestion
{
public:
    struct Answer
    {
        QString id;
        QString label;
    };
    using Chosen = std::function<void(const QString &id)>;

    UpscaleQuestion();
    ~UpscaleQuestion();

    /**
     * Shows @p text with @p answers on @p output. Escape chooses @p cancel.
     * Nothing is shown, and false returned, while another question is open or
     * while another effect holds the keyboard.
     */
    bool ask(Effect *owner, UpscaleOutput *output, const QString &text, const QList<Answer> &answers, const QString &cancel, const Chosen &chosen);
    bool isOpen() const;
    /** The question while it is open; empty otherwise. */
    QString text() const;
    /** A key while the question holds the keyboard. */
    void key(QKeyEvent *event);
    /** The pointer, at a global logical position, while the question holds it. */
    void pointerMoved(const QPointF &position);
    void pointerReleased(const QPointF &position);
    /** Where the answers were last drawn, in global logical coordinates. */
    QList<QRectF> answerAreas() const;
    void paint(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen);
    /** Takes the question away unanswered, as when its output goes. */
    void close();

private:
    void select(int index);
    void choose(const QString &id);

    Effect *m_owner = nullptr;
    QPointer<UpscaleOutput> m_output;
    UpscaleOverlay m_text;
    std::vector<std::unique_ptr<UpscaleOverlay>> m_buttons;
    QList<QRectF> m_areas;
    QList<Answer> m_answers;
    QString m_cancel;
    Chosen m_chosen;
    int m_selected = 0;
};

} // namespace KWin
