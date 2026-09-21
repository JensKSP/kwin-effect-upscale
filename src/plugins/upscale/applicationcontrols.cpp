/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The settings page's application list: the editor, what the list is, and the
// controls that act on the list as a whole.

#include "upscale_config.h"

#include "applicationeditor.h"

#include <KLocalizedString>

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

namespace KWin
{

void UpscaleEffectConfig::addApplicationControls(QFormLayout *layout)
{
    m_editor = new UpscaleApplicationEditor(widget());
    layout->addRow(m_editor);
    connect(m_editor, &UpscaleApplicationEditor::changed, this, [this]() {
        setNeedsSave(true);
        updateApplicationSummary();
    });

    m_applications = new QLabel(widget());
    m_applications->setObjectName(QStringLiteral("applicationSummary"));
    m_applications->setTextFormat(Qt::PlainText);
    m_applications->setWordWrap(true);
    m_resetApplications = new QPushButton(i18n("Restore Defaults"), widget());
    m_resetApplications->setObjectName(QStringLiteral("resetApplications"));
    auto exportList = new QPushButton(i18n("Export…"), widget());
    exportList->setObjectName(QStringLiteral("exportApplications"));
    auto importList = new QPushButton(i18n("Import…"), widget());
    importList->setObjectName(QStringLiteral("importApplications"));
    // What the list is, the way back to the list the package ships, and the
    // way to take it elsewhere. All three act on the games and never on "All
    // applications", whose values System Settings' own Defaults restores.
    auto status = new QHBoxLayout;
    status->addWidget(m_applications, 1);
    status->addWidget(exportList);
    status->addWidget(importList);
    status->addWidget(m_resetApplications);
    layout->addRow(status);
    connect(m_resetApplications, &QPushButton::clicked, this, &UpscaleEffectConfig::resetApplications);
    connect(exportList, &QPushButton::clicked, this, &UpscaleEffectConfig::exportApplications);
    connect(importList, &QPushButton::clicked, this, &UpscaleEffectConfig::importApplications);
    updateApplicationSummary();
}

void UpscaleEffectConfig::updateApplicationSummary()
{
    const bool customized = UpscaleApplicationEditor::customized();
    m_applications->setText(customized
                                ? i18n("Contains your changes.")
                                : i18n("Default list, updated with each release."));
    m_resetApplications->setEnabled(customized);
}

// The application list is a different kind of setting from the rest of this
// page: it is a list the effect ships and the user edits, kept in its own file
// so that a new package can deliver a corrected entry without touching what
// the user changed. Its restore is therefore separate from this page's
// Defaults, which restores the values above and leaves the list alone.
void UpscaleEffectConfig::resetApplications()
{
    // A different file than Apply writes, and not recoverable afterwards, so
    // the editor asks before doing it and does it at once.
    m_editor->restoreDefaults();
    updateApplicationSummary();
    reconfigureEffect();
}

// The list as it stands, pending edits included, in kwinupscalerc's own
// format, so that it can be kept, shared or moved to another installation.
void UpscaleEffectConfig::exportApplications()
{
    const QString path = QFileDialog::getSaveFileName(widget(), i18n("Export Application List"),
                                                      QStringLiteral("upscale-applications.conf"),
                                                      i18n("Application lists (*.conf)"));
    if (!path.isEmpty() && !m_editor->exportTo(path)) {
        QMessageBox::warning(widget(), i18n("Export Application List"), i18n("The list could not be written to %1.", path));
    }
}

// Imported entries are edits like any other: they are stored on Apply.
void UpscaleEffectConfig::importApplications()
{
    const QString path = QFileDialog::getOpenFileName(widget(), i18n("Import Application List"), QString(),
                                                      i18n("Application lists (*.conf)"));
    if (!path.isEmpty() && m_editor->importFrom(path) == 0) {
        QMessageBox::warning(widget(), i18n("Import Application List"), i18n("%1 describes no applications.", path));
    }
}

} // namespace KWin
