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
#include <QDBusReply>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <ranges>

namespace KWin
{

static const std::array<UpscaleControlMethod, 4> controlMethods = {
    UpscaleControlMethod::None,
    UpscaleControlMethod::AdvertisedMode,
    UpscaleControlMethod::AdvertisedScale,
    UpscaleControlMethod::AdvertisedModeAndScale,
};

static const std::array<ResolutionPreset, 7> resolutionPresets = {
    ResolutionPreset::Automatic,
    ResolutionPreset::Native,
    ResolutionPreset::UltraQuality,
    ResolutionPreset::Quality,
    ResolutionPreset::Balanced,
    ResolutionPreset::Performance,
    ResolutionPreset::Custom,
};

static QString presetLabel(ResolutionPreset preset)
{
    switch (preset) {
    case ResolutionPreset::Automatic:
        return i18n("Follow the global setting");
    case ResolutionPreset::Native:
        return i18n("Native");
    case ResolutionPreset::UltraQuality:
        return i18n("Ultra Quality");
    case ResolutionPreset::Quality:
        return i18n("Quality");
    case ResolutionPreset::Balanced:
        return i18n("Balanced");
    case ResolutionPreset::Performance:
        return i18n("Performance");
    case ResolutionPreset::Custom:
        return i18n("Custom");
    }
    return QString();
}

void UpscaleApplicationEditor::buildDetails(QFormLayout *form)
{
    for (const UpscaleControlMethod method : controlMethods) {
        m_method->addItem(describeControlMethod(method));
    }
    for (const ResolutionPreset preset : resolutionPresets) {
        m_preset->addItem(presetLabel(preset));
    }
    m_note->setWordWrap(true);
    m_note->setTextFormat(Qt::PlainText);
    m_windowClass->setPlaceholderText(i18n("Any"));
    m_instance->setPlaceholderText(i18n("Any"));
    m_program->setPlaceholderText(i18n("File name of the program"));
    form->addRow(i18n("Name:"), m_name);
    form->addRow(i18n("Window class:"), m_windowClass);
    form->addRow(i18n("Window instance:"), m_instance);
    form->addRow(i18n("Program:"), m_program);
    form->addRow(i18n("Resolution request:"), m_method);
    form->addRow(i18n("Resolution:"), m_preset);
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
    for (QComboBox *box : {m_method, m_preset}) {
        connect(box, &QComboBox::activated, this, [this]() {
            applyToSelected();
        });
    }
    connect(m_enabled, &QCheckBox::clicked, this, [this]() {
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
    , m_method(new QComboBox(this))
    , m_preset(new QComboBox(this))
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
    m_method->setObjectName(QStringLiteral("applicationMethod"));
    m_preset->setObjectName(QStringLiteral("applicationPreset"));
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
        auto *item = new QListWidgetItem(application.shipped
                                             ? application.name
                                             : i18nc("An application the user added", "%1 (yours)", application.name),
                                         m_list);
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
    const std::array<QWidget *, 7> fields = {m_name, m_windowClass, m_instance, m_program, m_method, m_preset, m_enabled};
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
    m_method->setCurrentIndex(int(std::ranges::distance(controlMethods.begin(), std::ranges::find(controlMethods, application->method))));
    m_preset->setCurrentIndex(int(std::ranges::distance(resolutionPresets.begin(), std::ranges::find(resolutionPresets, application->preset))));
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
    application->method = controlMethods.at(size_t(std::max(0, m_method->currentIndex())));
    application->preset = resolutionPresets.at(size_t(std::max(0, m_preset->currentIndex())));
    application->enabled = m_enabled->isChecked();
    const QScopedValueRollback updating(m_updating, true);
    if (QListWidgetItem *item = m_list->currentItem()) {
        item->setText(application->shipped ? application->name
                                           : i18nc("An application the user added", "%1 (yours)", application->name));
        item->setCheckState(application->enabled ? Qt::Checked : Qt::Unchecked);
    }
    Q_EMIT changed();
}

void UpscaleApplicationEditor::addApplication()
{
    UpscaleApplication application;
    application.name = i18n("New application");
    application.id = upscaleNewApplicationId(QStringLiteral("application"));
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
    // KWin performs the selection itself, for native and X11 windows alike.
    const QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                                QStringLiteral("org.kde.KWin"), QStringLiteral("queryWindowInfo"));
    QDBusReply<QVariantMap> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 60000);
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
    application.id = upscaleNewApplicationId(application.name.isEmpty() ? application.instance : application.name);
    application.order = m_applications.empty() ? 100 : m_applications.back().order + 10;
    m_applications.push_back(application);
    m_original.push_back(UpscaleApplication{});
    rebuildList();
    m_list->setCurrentRow(int(m_applications.size()) - 1);
    // The window gave its identity but not the program behind it, and the
    // request has to be made before any window exists.
    m_program->setFocus();
    Q_EMIT changed();
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

void UpscaleApplicationEditor::save()
{
    for (const QString &id : m_removed) {
        upscaleDeleteApplication(id);
    }
    m_removed.clear();
    for (size_t index = 0; index < m_applications.size(); ++index) {
        upscaleSaveApplication(m_applications[index], m_original[index]);
    }
    upscaleSyncApplications();
    load();
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
