/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "sliderfield.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cstdlib>

namespace KWin
{

UpscaleSliderField::UpscaleSliderField(QSlider *slider, QWidget *parent, int divisor)
    : QObject(slider)
    , m_widget(new QWidget(parent))
    , m_slider(slider)
    , m_field(new QDoubleSpinBox(m_widget))
    , m_divisor(divisor)
{
    // As many decimals as the divisor makes meaningful: two for a value held
    // in hundredths, none for one held whole.
    m_field->setDecimals(divisor >= 100 ? 2 : 0);
    m_field->setRange(double(slider->minimum()) / divisor, double(slider->maximum()) / divisor);
    m_field->setSingleStep(1);
    // Across the form's field column, as a slider on its own would be: a form
    // in KDE's style widens only the fields that ask to expand.
    m_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QHBoxLayout(m_widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_field);
    showSliderValue();
    connect(m_slider, &QSlider::rangeChanged, this, [this](int minimum, int maximum) {
        m_field->setRange(double(minimum) / m_divisor, double(maximum) / m_divisor);
    });
    // The slider is the value; the field follows it and sets it. A value
    // typed with more precision than the slider holds is rounded to it.
    connect(m_slider, &QSlider::valueChanged, this, &UpscaleSliderField::showSliderValue);
    connect(m_field, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        m_slider->setValue(qRound(value * m_divisor));
    });
    connect(m_slider, &QSlider::actionTriggered, this, &UpscaleSliderField::snap);
}

QWidget *UpscaleSliderField::widget() const
{
    return m_widget;
}

QDoubleSpinBox *UpscaleSliderField::field() const
{
    return m_field;
}

void UpscaleSliderField::showSliderValue()
{
    const QSignalBlocker blocker(m_field);
    m_field->setValue(double(m_slider->value()) / m_divisor);
}

void UpscaleSliderField::setSnapPoints(const QList<int> &points, int step)
{
    m_points = points;
    m_step = std::max(1, step);
}

// Called for every move the user makes - a drag, a key, the wheel - with the
// position the move asks for, before the slider takes it. Changing the
// position here is what Qt provides this signal for.
void UpscaleSliderField::snap()
{
    const int wanted = m_slider->sliderPosition();
    int landed = qRound(double(wanted) / m_step) * m_step;
    int closest = m_step;
    for (const int point : std::as_const(m_points)) {
        if (const int distance = std::abs(point - wanted); distance < closest) {
            closest = distance;
            landed = point;
        }
    }
    m_slider->setSliderPosition(std::clamp(landed, m_slider->minimum(), m_slider->maximum()));
}

} // namespace KWin
