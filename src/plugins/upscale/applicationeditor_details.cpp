/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The details of the selected entry: a game's tabs, how they show an entry,
// and how an edit goes back into it. Apart from the rest of the editor, which
// keeps the list itself, because this is the part that grows with every
// setting a game can state.

#include "applicationeditor.h"

#include "matching.h"
#include "resolutionpreview.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace KWin
{

// The lists the editor used to carry - every method, every preset, and a label
// for each - live in the tables now. A profile's fields are built from those
// rather than written out here, which is what keeps this file able to take
// another setting without passing the size limit.

// One tab of the details. The editor sits inside the page's own Applications
// box, and boxes inside a box are frames within frames; tabs keep the same
// sections, show one at a time, and keep the editor as short as its list.
static QFormLayout *addTab(QTabWidget *tabs, const QString &title)
{
    auto *page = new QWidget(tabs);
    auto *form = new QFormLayout(page);
    tabs->addTab(page, title);
    return form;
}

// Sectioned as the settings page is, so that a game's form reads like the
// global one with a Global choice added to each preference.
void UpscaleApplicationEditor::buildDetails(QVBoxLayout *details)
{
    m_note->setWordWrap(true);
    m_note->setTextFormat(Qt::PlainText);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("applicationDetails"));
    m_details = new QStackedWidget(this);
    m_details->addWidget(tabs);
    details->addWidget(m_details);
    QFormLayout *identification = addTab(tabs, i18n("Identification"));
    identification->addRow(i18n("Name:"), m_name);
    m_identity->build(identification, this);
    identification->addRow(QString(), m_enabled);
    identification->addRow(QString(), m_note);
    QFormLayout *requests = addTab(tabs, i18n("Resolution Request"));
    // What to ask for, per way the game can present itself. No Global choice
    // here: a method is a measurement of this program and has nothing to
    // inherit from anything else.
    m_methods->build(requests, this);
    // And what the user wants, every entry of which may follow the global
    // value instead.
    // Laid out as "All applications" lays out the same tab, and previewed
    // the same way, from the values this entry would use.
    QFormLayout *resolution = addTab(tabs, i18n("Resolution"));
    m_settings->build(resolution, this, {UpscaleSetting::Resolution, UpscaleSetting::Percentage});
    m_preview->build(resolution, this, QStringLiteral("applicationPreview"));
    m_settings->build(resolution, this, {UpscaleSetting::MinimumPixels});
    m_settings->build(addTab(tabs, i18n("Sharpening")), this, {UpscaleSetting::Sharpening, UpscaleSetting::Strength});
    // A display at a time, each switch followed by what it decides and set a
    // little apart from the next, as "All applications" groups them.
    QFormLayout *display = addTab(tabs, i18n("On-Screen Display"));
    const auto gap = [this, display]() {
        display->addItem(new QSpacerItem(0, fontMetrics().height() / 2, QSizePolicy::Minimum, QSizePolicy::Fixed));
    };
    m_settings->build(display, this,
                      {UpscaleSetting::OsdDetection, UpscaleSetting::OsdSummary, UpscaleSetting::OsdTimeout,
                       UpscaleSetting::AnnouncementPosition});
    gap();
    m_settings->build(display, this, {UpscaleSetting::OsdStatistics, UpscaleSetting::StatisticsPosition});
    gap();
    m_settings->build(display, this, {UpscaleSetting::OsdDeveloper, UpscaleSetting::DeveloperPosition});
}

void UpscaleApplicationEditor::showSelected()
{
    const QScopedValueRollback updating(m_updating, true);
    const UpscaleApplication *application = selected();
    const bool valid = application != nullptr;
    // "All applications" shows its own panel; any other row a game's tabs.
    m_details->setCurrentIndex(!valid && m_details->count() > 1 ? 1 : 0);
    m_name->setEnabled(valid);
    m_enabled->setEnabled(valid);
    m_identity->setEnabled(valid);
    // An entry this build ships comes back with the next package, so removing
    // it would not remove anything. Switching it off is what persists.
    m_delete->setEnabled(valid && !application->shipped);
    // Nothing moves above "All applications", which is not a match to order.
    const int row = m_list->currentRow();
    m_up->setEnabled(valid && row > rowOf(0));
    m_down->setEnabled(valid && row + 1 < m_list->count());
    if (!valid) {
        m_note->clear();
        return;
    }
    m_name->setText(application->name);
    m_identity->show(*application);
    m_methods->show(application->methods);
    m_settings->show(application->overrides, m_global);
    m_enabled->setChecked(application->enabled);
    showNote(*application);
    updatePreview();
}

void UpscaleApplicationEditor::setGlobalSettings(const UpscaleSettings &global)
{
    m_global = global;
    // Only a game's tabs name the global values. While "All applications" is
    // shown they are hidden, and selecting a game shows it afresh.
    if (selected()) {
        showSelected();
    }
}

// Computed from the values the entry would use: its own where it states one,
// the global ones where it follows them.
void UpscaleApplicationEditor::updatePreview()
{
    const UpscaleApplication *application = selected();
    if (!application) {
        return;
    }
    const auto effective = [this, application](UpscaleSetting setting) {
        return application->overrides[std::size_t(setting)].value_or(m_global.value(setting));
    };
    m_preview->show(ResolutionPreset(effective(UpscaleSetting::Resolution)), effective(UpscaleSetting::Percentage),
                    effective(UpscaleSetting::MinimumPixels));
}

void UpscaleApplicationEditor::showNote(const UpscaleApplication &application)
{
    // An entry that can never match says so where it is being edited, before
    // anything else about it: nothing else it says applies while it cannot.
    // One that is only incomplete says what it lacks, in the same place.
    QString problem = upscaleIdentityProblem(application);
    if (problem.isEmpty()) {
        problem = upscaleAdvertisementProblem(application);
    }
    if (!problem.isEmpty()) {
        m_note->setText(problem);
    } else {
        m_note->setText(application.shipped ? application.note : i18n("Added by you."));
    }
}

void UpscaleApplicationEditor::applyToSelected()
{
    if (m_updating) {
        return;
    }
    UpscaleApplication *application = selected();
    if (!application) {
        return;
    }
    application->name = m_name->text();
    m_identity->store(*application);
    showNote(*application);
    m_methods->store(application->methods);
    m_settings->store(application->overrides);
    updatePreview();
    application->enabled = m_enabled->isChecked();
    const QScopedValueRollback updating(m_updating, true);
    if (QListWidgetItem *item = m_list->currentItem()) {
        item->setText(application->name);
        item->setCheckState(application->enabled ? Qt::Checked : Qt::Unchecked);
    }
    Q_EMIT changed();
}

} // namespace KWin
