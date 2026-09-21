/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "settings.h"

#include <QObject>
#include <QString>

#include <vector>

class QFormLayout;
class QWidget;

namespace KWin
{

/**
 * The controls for preferences a profile may override, built from the table.
 *
 * Every inheritable preference offers the same three-way choice: follow the
 * global value, or state one of this game's own. A switch therefore has three
 * positions rather than two, and a number reserves one step below its range
 * for the same purpose. Both show what the global value currently is, so that
 * choosing to follow it is an informed choice rather than a blank.
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

    /** Show @p overrides, naming the values from @p global where absent. */
    void show(const UpscaleSettingOverrides &overrides, const UpscaleSettings &global);

    /** Read the controls back into @p overrides. */
    void store(UpscaleSettingOverrides &overrides) const;

    /** Whether any control is currently stating a value of its own. */
    bool anyStated() const;

    /** Put every control back to following the global value. */
    void clear();

Q_SIGNALS:
    /** A control changed, for the page's own dirty tracking. */
    void changed();

private:
    struct Control;
    std::vector<Control> m_controls;
};

/** The label for one value of a preference that offers a list of them. */
QString upscaleSettingChoiceLabel(UpscaleSetting setting, int value);

/** How many values a Choice preference offers, for building its list. */
int upscaleSettingChoiceCount(UpscaleSetting setting);

/** The label a form puts beside this preference's control. */
QString upscaleSettingLabel(UpscaleSetting setting);

} // namespace KWin
