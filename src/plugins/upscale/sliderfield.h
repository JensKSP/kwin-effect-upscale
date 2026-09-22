/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QObject>

class QDoubleSpinBox;
class QSlider;
class QWidget;

namespace KWin
{

/**
 * A slider with its exact value beside it, which a person can also type.
 *
 * The slider is for moving by feel, the field for the exact number. Both show
 * one value, held in the slider's integer steps; the field divides it for
 * display, so that the scale can be held in basis points and shown as
 * 66.67 %. The slider stays the control the page reads and connects to.
 *
 * Optionally the slider snaps. Dragged or stepped to within one step of a
 * snap point it lands on the point exactly, and anywhere else on a whole step,
 * so a drag gives round values and the values that matter without having to
 * hit either. Typing in the field never snaps: typed is exact.
 */
class UpscaleSliderField : public QObject
{
    Q_OBJECT

public:
    /**
     * Pair @p slider with a field showing its value divided by @p divisor.
     *
     * Both move into one widget, a child of @p parent, so that the pair has
     * an owner whether or not a form ever takes it.
     */
    UpscaleSliderField(QSlider *slider, QWidget *parent, int divisor);

    /** The slider and the field, side by side, for a form's field column. */
    QWidget *widget() const;

    /** The field, for naming and for its special text. */
    QDoubleSpinBox *field() const;

    /** Show the slider's value in the field after it was set with signals blocked. */
    void showSliderValue();

    /** Snap to @p points, in the slider's steps; between them move by @p step. */
    void setSnapPoints(const QList<int> &points, int step);

private:
    void snap();

    QWidget *m_widget;
    QSlider *m_slider;
    QDoubleSpinBox *m_field;
    int m_divisor;
    QList<int> m_points;
    int m_step = 1;
};

} // namespace KWin
