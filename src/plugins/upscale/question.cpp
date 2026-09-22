/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "question.h"
#include "warningtext.h"

#include "effect/effecthandler.h"

#include <QKeyEvent>

namespace KWin
{

// Larger than the diagnostic blocks: it is read from a sofa as well.
static constexpr double questionEmphasis = 1.25;
// Logical pixels between the question and its answers, and between answers.
static constexpr double questionGap = 24;

UpscaleQuestion::UpscaleQuestion() = default;
UpscaleQuestion::~UpscaleQuestion()
{
    close();
}

bool UpscaleQuestion::ask(Effect *owner, UpscaleOutput *output, const QString &text, const QList<Answer> &answers, const QString &cancel,
                          const Chosen &chosen)
{
    if (isOpen() || !output || answers.isEmpty() || !effects->grabKeyboard(owner)) {
        return false;
    }
    effects->startMouseInterception(owner, Qt::ArrowCursor);
    m_owner = owner;
    m_output = output;
    m_answers = answers;
    m_cancel = cancel;
    m_chosen = chosen;
    m_text.setText(text, output->scale(), questionEmphasis);
    m_buttons.clear();
    for (qsizetype index = 0; index < answers.size(); ++index) {
        m_buttons.push_back(std::make_unique<UpscaleOverlay>());
    }
    select(0);
    return true;
}

bool UpscaleQuestion::isOpen() const
{
    return m_owner != nullptr;
}

QString UpscaleQuestion::text() const
{
    return isOpen() ? m_text.text() : QString();
}

void UpscaleQuestion::select(int index)
{
    const int count = int(m_answers.size());
    m_selected = (index % count + count) % count;
    const double scale = m_output ? m_output->scale() : 1;
    for (int answer = 0; answer < count; ++answer) {
        const QString label = m_answers.at(answer).label;
        m_buttons[size_t(answer)]->setText(answer == m_selected ? upscaleHighlight(label) : label, scale, questionEmphasis);
    }
    effects->addRepaintFull();
}

void UpscaleQuestion::key(QKeyEvent *event)
{
    if (!isOpen() || event->type() != QEvent::KeyPress) {
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Backtab:
        select(m_selected - 1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Tab:
        select(m_selected + 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        choose(m_answers.at(m_selected).id);
        break;
    case Qt::Key_Escape:
        choose(m_cancel);
        break;
    default:
        break;
    }
}

void UpscaleQuestion::pointerMoved(const QPointF &position)
{
    for (int answer = 0; answer < int(m_areas.size()); ++answer) {
        if (m_areas.at(answer).contains(position) && answer != m_selected) {
            select(answer);
            return;
        }
    }
}

void UpscaleQuestion::pointerReleased(const QPointF &position)
{
    for (int answer = 0; answer < int(m_areas.size()); ++answer) {
        if (m_areas.at(answer).contains(position)) {
            choose(m_answers.at(answer).id);
            return;
        }
    }
}

QList<QRectF> UpscaleQuestion::answerAreas() const
{
    return m_areas;
}

void UpscaleQuestion::choose(const QString &id)
{
    const Chosen chosen = m_chosen;
    close();
    if (chosen) {
        chosen(id);
    }
}

void UpscaleQuestion::close()
{
    if (!isOpen()) {
        return;
    }
    Effect *owner = m_owner;
    m_owner = nullptr;
    m_chosen = nullptr;
    m_areas.clear();
    effects->ungrabKeyboard();
    effects->stopMouseInterception(owner);
    m_text.release();
    m_buttons.clear();
    effects->addRepaintFull();
}

void UpscaleQuestion::paint(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen)
{
    if (!isOpen() || screen != m_output) {
        return;
    }
    const auto area = screen->geometryF();
    // Room for the question, and the row of answers below it, centred.
    m_text.fit(QSizeF(area.width() * 0.7, area.height() * 0.6));
    double rowWidth = questionGap * double(m_buttons.size() - 1);
    double rowHeight = 0;
    for (const std::unique_ptr<UpscaleOverlay> &button : m_buttons) {
        button->fit(QSizeF(area.width() * 0.7 / double(m_buttons.size()), area.height() * 0.2));
        rowWidth += button->size().width();
        rowHeight = std::max(rowHeight, button->size().height());
    }
    const QSizeF text = m_text.size();
    const double centre = area.x() + (area.width() / 2);
    const double top = area.y() + ((area.height() - text.height() - questionGap - rowHeight) / 2);
    m_text.paint(target, viewport, QPointF(centre - (text.width() / 2), top));
    double left = centre - (rowWidth / 2);
    m_areas.clear();
    for (const std::unique_ptr<UpscaleOverlay> &button : m_buttons) {
        const QPointF position(left, top + text.height() + questionGap);
        button->paint(target, viewport, position);
        m_areas.append(QRectF(position, button->size()));
        left += button->size().width() + questionGap;
    }
}

} // namespace KWin
