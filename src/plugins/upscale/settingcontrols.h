/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <QObject>
#include <QString>

#include <optional>
#include <vector>

class QFormLayout;
class QWidget;

namespace KWin
{

/**
 * The controls for preferences a profile may override, built from the table.
 *
 * A game's page looks like the global one: the same kind of control for each
 * preference, and each showing the value the game would use. It follows Qt
 * Designer's property editor, as Jens decided on 2026-09-21: what a game
 * states for itself has its name in bold and an enabled reset button at the
 * end of its row, which makes it follow the global value again; everything
 * else follows the global value, changes with it, and is shown in italic. That replaced a
 * "Global (value)" first choice in every control, which made each one longer.
 *
 * Changing a control states its value. The scale and the preset move together
 * as on the global page: stating a scale chooses Custom, and any other preset
 * puts the scale back to following, since it would no longer apply. Nothing is
 * greyed out by another setting being off, here or there: a value set while
 * its switch is off is simply waiting for it.
 *
 * Built from upscaleSettingTable() rather than written out per preference.
 * That is not tidiness: the profile editor and the settings page would
 * otherwise need one control, one reader and one writer each for fifteen
 * preferences, and upscale_config.cpp is already within fifty lines of the
 * size limit this project enforces. A table-driven form costs one row per
 * preference and nothing per file.
 */
class UpscaleSettingControls : public QObject
{
    Q_OBJECT

public:
    // Complete only in the source, which keeps the widget details out of this
    // header; named here so that the source's helpers can take one.
    struct Control;

    explicit UpscaleSettingControls(QObject *parent = nullptr);
    // Defined where Control is complete: the vector below holds an incomplete
    // type here, which is the point of keeping the widget details out of this
    // header.
    ~UpscaleSettingControls() override;

    /**
     * Add a control per entry of @p settings to @p form.
     *
     * The order is the caller's, because a form is read top to bottom by a
     * person and the table's order is the storage's.
     */
    void build(QFormLayout *form, QWidget *parent, const std::vector<UpscaleSetting> &settings);

    /** Show @p overrides, and the values from @p global where absent. */
    void show(const UpscaleSettingOverrides &overrides, const UpscaleSettings &global);

    /** Read the controls back into @p overrides. */
    void store(UpscaleSettingOverrides &overrides) const;

Q_SIGNALS:
    /** A control changed, for the page's own dirty tracking. */
    void changed();

private:
    const Control *find(UpscaleSetting setting) const;
    Control *controlFor(UpscaleSetting setting);
    /** Create @p control's widget, connected, and return what the form holds. */
    QWidget *field(Control &control, QWidget *parent);
    /** What a control shows while it follows the global value. */
    int followed(UpscaleSetting setting) const;
    /** Show whether @p control states its value: a bold name and an enabled reset. */
    static void mark(const Control &control);
    /** A person changed a control, which states its value. */
    void userEdited(UpscaleSetting setting);
    /** The reset button: follow the global value again. */
    void follow(UpscaleSetting setting);

    std::vector<Control> m_controls;
    // The global values the controls follow, as last shown.
    UpscaleSettings m_global;
    // Set while show() fills the controls, whose own signals must not be
    // taken for a person's choice.
    bool m_showing = false;
};

/**
 * Show whether a game states a value itself (@p own) or inherits it: its
 * @p name in bold and its @p reset enabled when it is its own, as Qt Designer
 * marks a changed property, and its @p value in italic when inherited.
 */
void upscaleMarkInherited(QWidget *name, QWidget *value, QWidget *reset, bool own);

/** The label for one value of a preference that offers a list of them. */
QString upscaleSettingChoiceLabel(UpscaleSetting setting, int value);

/** How many values a Choice preference offers, for building its list. */
int upscaleSettingChoiceCount(UpscaleSetting setting);

/** The label a form puts beside this preference's control. */
QString upscaleSettingLabel(UpscaleSetting setting);

} // namespace KWin
