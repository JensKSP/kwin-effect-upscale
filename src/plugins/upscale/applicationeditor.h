/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "identitycontrols.h"
#include "methodcontrols.h"
#include "settingcontrols.h"

#include <QPointer>
#include <QVariantMap>
#include <QWidget>

#include <vector>

class QCheckBox;
class QDBusPendingCallWatcher;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

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
    /**
     * Write what the user changed.
     *
     * Returns false without writing anything when an entry could never match
     * a window, so that the settings page keeps it and stays applicable.
     */
    bool save();
    /** Put the list back to the one this build ships, after asking. */
    void restoreDefaults();
    /** Whether the stored list differs from the one this build ships. */
    static bool customized();

Q_SIGNALS:
    /** A field changed, so the settings page has something to apply. */
    void changed();

private:
    void buildDetails(QVBoxLayout *details);
    void connectControls();
    void rebuildList();
    void showSelected();
    void showNote(const UpscaleApplication &application);
    void applyToSelected();
    void addApplication();
    void addFromWindow();
    void askForProgram();
    void addIdentified(const QVariantMap &information, const QString &executable);
    void deleteSelected();
    void moveSelected(int step);
    UpscaleApplication *selected();

    std::vector<UpscaleApplication> m_applications;
    // What each entry looked like when it was read, so that saving can store
    // the difference rather than a copy of the shipped values.
    std::vector<UpscaleApplication> m_original;
    std::vector<QString> m_removed;

    QListWidget *m_list;
    QLineEdit *m_name;
    UpscaleIdentityControls *m_identity;
    // The six measured answers, and the preferences this profile may state
    // of its own. Both are built from a table rather than written out field
    // by field, which is what keeps this file able to take another setting.
    UpscaleMethodControls *m_methods;
    UpscaleSettingControls *m_settings;
    QCheckBox *m_enabled;
    QLabel *m_note;
    QPushButton *m_delete;
    QPushButton *m_up;
    QPushButton *m_down;
    // The window KWin's picker returned, while the effect is asked its program.
    // A second pick waits for both: m_selecting while the picker is open, the
    // query while the effect answers, so that it cannot replace m_picked.
    QVariantMap m_picked;
    QPointer<QDBusPendingCallWatcher> m_programQuery;
    bool m_selecting = false;
    bool m_updating = false;
};

} // namespace KWin
