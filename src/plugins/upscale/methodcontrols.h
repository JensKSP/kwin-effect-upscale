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
class QLabel;
class QToolButton;
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

    /**
     * Add the six rows to @p form.
     *
     * A game's rows @p inherit: each follows a parent until the game states
     * a method of its own, and shows which of the two it is, the way a game's
     * other settings do (see UpscaleSettingControls). The global profile's
     * rows have no parent and are plain lists.
     */
    void build(QFormLayout *form, QWidget *parent, bool inherit = false);
    /** The global profile's six. */
    void show(const UpscaleMethods &methods);
    void store(UpscaleMethods &methods) const;
    /**
     * A game's six: its own where it states one, otherwise the package's
     * measurement from @p measured, and otherwise the @p global answer.
     */
    void show(const UpscaleStatedMethods &methods, const UpscaleStatedMethods &measured, const UpscaleMethods &global);
    /** Read a game's six back: its own, or what @p measured states. */
    void store(UpscaleStatedMethods &methods, const UpscaleStatedMethods &measured) const;
    /** Offer the six boxes, or show them as not applying. */
    void setEnabled(bool enabled);

Q_SIGNALS:
    void changed();

private:
    void select(std::size_t slot, UpscaleMethod method);
    void mark(std::size_t slot);
    void edited(std::size_t slot);
    void follow(std::size_t slot);

    std::array<QComboBox *, upscalePresentationCount> m_boxes{};
    std::array<std::vector<UpscaleMethod>, upscalePresentationCount> m_offered;
    // Only for a game's rows: the row's label, its reset button, whether the
    // game states the slot itself, and what the slot follows when it does not.
    std::array<QLabel *, upscalePresentationCount> m_names{};
    std::array<QToolButton *, upscalePresentationCount> m_resets{};
    std::array<bool, upscalePresentationCount> m_own{};
    std::array<UpscaleMethod, upscalePresentationCount> m_parents{};
    bool m_showing = false;
};

/** One sentence naming what the effect does for this presentation. */
QString upscalePresentationLabel(UpscalePresentation presentation);

/** The name of a method as the settings page shows it. */
QString upscaleMethodLabel(UpscaleMethod method);

} // namespace KWin
