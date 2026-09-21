/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "applicationeditor.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>

namespace KWin
{

// The lists this file used to carry - every method, every preset, and a label
// for each - live in the tables now. A profile's fields are built from those
// rather than written out here, which is what keeps this file able to take
// another setting without passing the size limit.

void UpscaleApplicationEditor::buildDetails(QFormLayout *form)
{

    m_note->setWordWrap(true);
    m_note->setTextFormat(Qt::PlainText);
    m_windowClass->setPlaceholderText(i18n("Any"));
    m_instance->setPlaceholderText(i18n("Any"));
    m_program->setPlaceholderText(i18n("File name of the program"));
    form->addRow(i18n("Name:"), m_name);
    form->addRow(i18n("Window class:"), m_windowClass);
    form->addRow(i18n("Window instance:"), m_instance);
    form->addRow(i18n("Program:"), m_program);
    // What to ask for, per way the game can present itself. No "use global"
    // here: a method is a measurement of this program and has nothing to
    // inherit from anything else.
    m_methods->build(form, this);
    // And what the user wants, every entry of which may follow the global
    // value instead.
    m_settings->build(form, this,
                      {UpscaleSetting::Resolution, UpscaleSetting::Percentage, UpscaleSetting::MinimumPixels,
                       UpscaleSetting::Sharpening, UpscaleSetting::Strength, UpscaleSetting::ResolutionControl,
                       UpscaleSetting::OsdDetection, UpscaleSetting::OsdSummary, UpscaleSetting::OsdStatistics,
                       UpscaleSetting::OsdDeveloper, UpscaleSetting::OsdTimeout,
                       UpscaleSetting::AnnouncementPosition, UpscaleSetting::StatisticsPosition,
                       UpscaleSetting::DeveloperPosition});
    form->addRow(QString(), m_enabled);
    form->addRow(QString(), m_note);
}

void UpscaleApplicationEditor::connectControls()
{
    connect(m_list, &QListWidget::currentRowChanged, this, [this]() {
        showSelected();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_updating) {
            return;
        }
        const int row = m_list->row(item);
        if (row >= 0 && size_t(row) < m_applications.size()) {
            m_applications[row].enabled = item->checkState() == Qt::Checked;
            showSelected();
            Q_EMIT changed();
        }
    });
    for (QLineEdit *edit : {m_name, m_windowClass, m_instance, m_program}) {
        connect(edit, &QLineEdit::textEdited, this, [this]() {
            applyToSelected();
        });
    }
    connect(m_enabled, &QCheckBox::clicked, this, [this]() {
        applyToSelected();
    });
    connect(m_methods, &UpscaleMethodControls::changed, this, [this]() {
        applyToSelected();
    });
    connect(m_settings, &UpscaleSettingControls::changed, this, [this]() {
        applyToSelected();
    });
}

UpscaleApplicationEditor::UpscaleApplicationEditor(QWidget *parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
    , m_name(new QLineEdit(this))
    , m_windowClass(new QLineEdit(this))
    , m_instance(new QLineEdit(this))
    , m_program(new QLineEdit(this))
    , m_methods(new UpscaleMethodControls(this))
    , m_settings(new UpscaleSettingControls(this))
    , m_enabled(new QCheckBox(i18n("Recognize this application"), this))
    , m_note(new QLabel(this))
{
    auto *form = new QFormLayout;
    buildDetails(form);

    auto *add = new QPushButton(i18n("Add…"), this);
    auto *detect = new QPushButton(i18n("Add from window…"), this);
    m_delete = new QPushButton(i18n("Remove"), this);
    // Named as the rest of the settings page names its controls, so that the
    // tests reach them the way they reach everything else on it.
    m_list->setObjectName(QStringLiteral("applicationList"));
    m_name->setObjectName(QStringLiteral("applicationName"));
    m_windowClass->setObjectName(QStringLiteral("applicationWindowClass"));
    m_instance->setObjectName(QStringLiteral("applicationInstance"));
    m_program->setObjectName(QStringLiteral("applicationProgram"));

    m_enabled->setObjectName(QStringLiteral("applicationEnabled"));
    m_note->setObjectName(QStringLiteral("applicationNote"));
    add->setObjectName(QStringLiteral("applicationAdd"));
    detect->setObjectName(QStringLiteral("applicationAddFromWindow"));
    m_delete->setObjectName(QStringLiteral("applicationRemove"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(detect);
    buttons->addWidget(m_delete);
    buttons->addStretch();

    auto *left = new QVBoxLayout;
    left->addWidget(m_list);
    left->addLayout(buttons);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(left, 1);
    layout->addLayout(form, 1);

    connectControls();
    connect(add, &QPushButton::clicked, this, &UpscaleApplicationEditor::addApplication);
    connect(detect, &QPushButton::clicked, this, &UpscaleApplicationEditor::addFromWindow);
    connect(m_delete, &QPushButton::clicked, this, &UpscaleApplicationEditor::deleteSelected);
    load();
}

void UpscaleApplicationEditor::load()
{
    upscaleReloadApplications();
    m_applications = upscaleApplications();
    m_original = m_applications;
    m_removed.clear();
    rebuildList();
}

void UpscaleApplicationEditor::rebuildList()
{
    const QScopedValueRollback updating(m_updating, true);
    const int row = m_list->currentRow();
    m_list->clear();
    for (const UpscaleApplication &application : m_applications) {
        // The name and nothing else. Where an entry came from is answered
        // where it matters - the note under the fields says "Added by you",
        // and Delete is only enabled for such an entry - so decorating every
        // name in the list with it charges the common case for the rare one.
        auto *item = new QListWidgetItem(application.name, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(application.enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_list->setCurrentRow(m_applications.empty() ? -1 : std::clamp(row, 0, int(m_applications.size()) - 1));
    showSelected();
}

UpscaleApplication *UpscaleApplicationEditor::selected()
{
    const int row = m_list->currentRow();
    return row >= 0 && size_t(row) < m_applications.size() ? &m_applications[row] : nullptr;
}

void UpscaleApplicationEditor::showSelected()
{
    const QScopedValueRollback updating(m_updating, true);
    const UpscaleApplication *application = selected();
    const bool valid = application != nullptr;
    const std::array<QWidget *, 5> fields = {m_name, m_windowClass, m_instance, m_program, m_enabled};
    for (QWidget *widget : fields) {
        widget->setEnabled(valid);
    }
    // An entry this build ships comes back with the next package, so removing
    // it would not remove anything. Switching it off is what persists.
    m_delete->setEnabled(valid && !application->shipped);
    if (!valid) {
        m_note->clear();
        return;
    }
    m_name->setText(application->name);
    m_windowClass->setText(application->windowClass);
    m_instance->setText(application->instance);
    m_program->setText(application->program);
    m_methods->show(application->methods);
    m_settings->show(application->overrides, upscaleGlobalSettings());
    m_enabled->setChecked(application->enabled);
    m_note->setText(application->shipped
                        ? application->note
                        : i18n("Added by you. A request this application does not follow will not make it "
                               "render less, and can stop its window covering the screen."));
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
    application->windowClass = m_windowClass->text();
    application->instance = m_instance->text();
    application->program = m_program->text();
    m_methods->store(application->methods);
    m_settings->store(application->overrides);
    application->enabled = m_enabled->isChecked();
    const QScopedValueRollback updating(m_updating, true);
    if (QListWidgetItem *item = m_list->currentItem()) {
        item->setText(application->name);
        item->setCheckState(application->enabled ? Qt::Checked : Qt::Unchecked);
    }
    Q_EMIT changed();
}

void UpscaleApplicationEditor::addApplication()
{
    UpscaleApplication application;
    application.name = i18n("New application");
    // Against this editor's own entries as well as the stored ones: two
    // Add operations before Apply would otherwise share a configuration
    // group, and one of them would be written over the other.
    application.id = upscaleNewApplicationId(QStringLiteral("application"), m_applications);
    // After everything shipped, so that a measured entry keeps deciding first.
    application.order = m_applications.empty() ? 100 : m_applications.back().order + 10;
    m_applications.push_back(application);
    m_original.push_back(UpscaleApplication{});
    rebuildList();
    m_list->setCurrentRow(int(m_applications.size()) - 1);
    m_name->setFocus();
    Q_EMIT changed();
}

void UpscaleApplicationEditor::addFromWindow()
{
    if (m_selecting) {
        return;
    }
    m_selecting = true;
    // Keep the settings event loop responsive during KWin's interactive picker.
    // The watcher is owned by this editor; closing it cancels our reply handler.
    // KWin performs the selection itself, for native and X11 windows alike.
    const QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                                QStringLiteral("org.kde.KWin"), QStringLiteral("queryWindowInfo"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        m_selecting = false;
        if (!reply.isValid()) {
            // Cancelling the selection is an error reply, and not a failure worth
            // a dialog. Anything else is worth saying out loud.
            if (reply.error().name() != QLatin1String("org.kde.KWin.Error.UserCancel")) {
                QMessageBox::warning(this, i18n("Add from window"),
                                     i18n("The window could not be identified: %1", reply.error().message()));
            }
            return;
        }
        const QVariantMap information = reply.value();
        UpscaleApplication application;
        application.name = information.value(QStringLiteral("resourceClass")).toString();
        application.windowClass = application.name;
        application.instance = information.value(QStringLiteral("resourceName")).toString();
        if (application.windowClass.isEmpty() && application.instance.isEmpty()) {
            QMessageBox::warning(this, i18n("Add from window"),
                                 i18n("That window reports no application identity, so it cannot be recognized."));
            return;
        }
        application.id = upscaleNewApplicationId(application.name.isEmpty() ? application.instance : application.name,
                                                 m_applications);
        application.order = m_applications.empty() ? 100 : m_applications.back().order + 10;
        m_applications.push_back(application);
        m_original.push_back(UpscaleApplication{});
        rebuildList();
        m_list->setCurrentRow(int(m_applications.size()) - 1);
        // The window gave its identity but not the program behind it, and the
        // request has to be made before any window exists.
        m_program->setFocus();
        Q_EMIT changed();
    });
}

void UpscaleApplicationEditor::deleteSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || size_t(row) >= m_applications.size() || m_applications[row].shipped) {
        return;
    }
    m_removed.push_back(m_applications[row].id);
    m_applications.erase(m_applications.begin() + row);
    m_original.erase(m_original.begin() + row);
    rebuildList();
    Q_EMIT changed();
}

bool UpscaleApplicationEditor::save()
{
    // An entry stating neither a window class nor an instance would match
    // every window on the screen, so the reader drops it. Writing it anyway
    // would make it disappear from this list on the next read without saying
    // why, and leave a group behind in the file that nothing describes.
    const auto nameless = std::ranges::find_if(m_applications, [](const UpscaleApplication &application) {
        return application.windowClass.isEmpty() && application.instance.isEmpty();
    });
    if (nameless != m_applications.end()) {
        m_list->setCurrentRow(int(std::ranges::distance(m_applications.begin(), nameless)));
        m_windowClass->setFocus();
        QMessageBox::warning(this, i18n("Applications"),
                             i18n("“%1” states neither a window class nor a window instance, so nothing could ever "
                                  "match it. Give it one of them, or remove it.",
                                  nameless->name));
        return false;
    }
    for (const QString &id : m_removed) {
        upscaleDeleteApplication(id);
    }
    m_removed.clear();
    for (size_t index = 0; index < m_applications.size(); ++index) {
        upscaleSaveApplication(m_applications[index], m_original[index]);
    }
    upscaleSyncApplications();
    load();
    return true;
}

bool UpscaleApplicationEditor::customized()
{
    return upscaleApplicationsCustomized();
}

void UpscaleApplicationEditor::restoreDefaults()
{
    const auto answer = QMessageBox::question(this, i18n("Restore the shipped application list"),
                                              i18n("Discard your own applications and every change you made to the shipped ones? "
                                                   "The list becomes the one this version of the effect ships."),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    upscaleRestoreApplications();
    load();
    Q_EMIT changed();
}

} // namespace KWin
