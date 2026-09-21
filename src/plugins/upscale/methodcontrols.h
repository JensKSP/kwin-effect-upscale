/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "presentation.h"

#include <QObject>
#include <QString>

#include <array>
#include <vector>

class QComboBox;
class QFormLayout;
class QWidget;

namespace KWin
{

/**
 * The six answers a profile gives, one per way its game can present itself.
 *
 * These are the only fields in the editor with no "use global": a method is a
 * measurement of one program, so there is nothing for it to inherit. What they
 * offer instead is Auto, which is also what an unmeasured slot reads as, and
 * Off, which records that the question was asked and the answer is to ask this
 * program for nothing.
 *
 * Each slot lists only the methods its protocol can carry, because an
 * advertisement cannot reach an X11 window and a resize cannot reach a Wayland
 * one. Offering all six everywhere would invite a person to store a value that
 * silently does nothing.
 */
class UpscaleMethodControls : public QObject
{
    Q_OBJECT

public:
    explicit UpscaleMethodControls(QObject *parent = nullptr);

    void build(QFormLayout *form, QWidget *parent);
    void show(const UpscaleMethods &methods);
    void store(UpscaleMethods &methods) const;
    /** Offer the six boxes, or show them as not applying. */
    void setEnabled(bool enabled);

Q_SIGNALS:
    void changed();

private:
    std::array<QComboBox *, upscalePresentationCount> m_boxes{};
    std::array<std::vector<UpscaleMethod>, upscalePresentationCount> m_offered;
};

/** One sentence naming what the effect does for this presentation. */
QString upscalePresentationLabel(UpscalePresentation presentation);

/** The name of a method as the settings page shows it. */
QString upscaleMethodLabel(UpscaleMethod method);

} // namespace KWin
