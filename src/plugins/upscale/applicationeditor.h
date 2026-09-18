/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace KWin
{

/**
 * The list of applications the effect recognizes, and what it does for each.
 *
 * Edits are held here until the settings page is applied, as the rest of that
 * page behaves, and written sparsely: only a field the user actually changed
 * is stored, so everything else keeps following the installed package.
 *
 * An application the effect ships cannot be deleted, only switched off. Its
 * entry comes back with the next package whatever this file says, so offering
 * to delete it would be offering something that does not happen.
 */
class UpscaleApplicationEditor : public QWidget
{
    Q_OBJECT

public:
    explicit UpscaleApplicationEditor(QWidget *parent = nullptr);

    /** Read the stored list again, discarding anything not applied. */
    void load();
    /** Write what the user changed. */
    void save();
    /** Put the list back to the one this build ships, after asking. */
    void restoreDefaults();
    /** Whether the stored list differs from the one this build ships. */
    static bool customized();

Q_SIGNALS:
    /** A field changed, so the settings page has something to apply. */
    void changed();

private:
    void buildDetails(QFormLayout *form);
    void connectControls();
    void rebuildList();
    void showSelected();
    void applyToSelected();
    void addApplication();
    void addFromWindow();
    void deleteSelected();
    UpscaleApplication *selected();

    std::vector<UpscaleApplication> m_applications;
    // What each entry looked like when it was read, so that saving can store
    // the difference rather than a copy of the shipped values.
    std::vector<UpscaleApplication> m_original;
    std::vector<QString> m_removed;

    QListWidget *m_list;
    QLineEdit *m_name;
    QLineEdit *m_windowClass;
    QLineEdit *m_instance;
    QLineEdit *m_program;
    QComboBox *m_method;
    QComboBox *m_preset;
    QCheckBox *m_enabled;
    QLabel *m_note;
    QPushButton *m_delete;
    bool m_updating = false;
};

} // namespace KWin
