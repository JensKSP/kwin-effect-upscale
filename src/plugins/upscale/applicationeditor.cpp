/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "applicationeditor.h"

#include "matching.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFormLayout>
#include <QGroupBox>
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

// One titled group of the details, the way the settings page groups its own.
static QFormLayout *addGroup(QVBoxLayout *details, QWidget *parent, const QString &title)
{
    auto *group = new QGroupBox(title, parent);
    auto *form = new QFormLayout(group);
    details->addWidget(group);
    return form;
}

// Grouped as the settings page is, so that a game's form reads like the
// global one with a Global choice added to each preference.
void UpscaleApplicationEditor::buildDetails(QVBoxLayout *details)
{
    m_note->setWordWrap(true);
    m_note->setTextFormat(Qt::PlainText);
    QFormLayout *identification = addGroup(details, this, i18n("Identification"));
    identification->addRow(i18n("Name:"), m_name);
    m_identity->build(identification, this);
    identification->addRow(QString(), m_enabled);
    identification->addRow(QString(), m_note);
    QFormLayout *requests = addGroup(details, this, i18n("Resolution Request"));
    m_settings->build(requests, this, {UpscaleSetting::ResolutionControl});
    // What to ask for, per way the game can present itself. No Global choice
    // here: a method is a measurement of this program and has nothing to
    // inherit from anything else.
    m_methods->build(requests, this);
    // And what the user wants, every entry of which may follow the global
    // value instead.
    m_settings->build(addGroup(details, this, i18n("Resolution")), this,
                      {UpscaleSetting::Resolution, UpscaleSetting::Percentage, UpscaleSetting::MinimumPixels});
    m_settings->build(addGroup(details, this, i18n("Sharpening")), this,
                      {UpscaleSetting::Sharpening, UpscaleSetting::Strength});
    m_settings->build(addGroup(details, this, i18n("On-Screen Display")), this,
                      {UpscaleSetting::OsdDetection, UpscaleSetting::OsdSummary, UpscaleSetting::OsdStatistics,
                       UpscaleSetting::OsdDeveloper, UpscaleSetting::OsdTimeout, UpscaleSetting::AnnouncementPosition,
                       UpscaleSetting::StatisticsPosition, UpscaleSetting::DeveloperPosition});
    details->addStretch();
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
    connect(m_name, &QLineEdit::textEdited, this, [this]() {
        applyToSelected();
    });
    connect(m_identity, &UpscaleIdentityControls::changed, this, [this]() {
        applyToSelected();
    });
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
    , m_identity(new UpscaleIdentityControls(this))
    , m_methods(new UpscaleMethodControls(this))
    , m_settings(new UpscaleSettingControls(this))
    , m_enabled(new QCheckBox(i18nc("An application profile takes part in matching", "Enabled"), this))
    , m_note(new QLabel(this))
{
    auto *details = new QVBoxLayout;
    buildDetails(details);

    // No ellipsis on Add: it adds an entry at once, and KDE keeps the
    // ellipsis for a button that asks for more before it acts. Picking a
    // window is such a step.
    auto *add = new QPushButton(i18n("Add"), this);
    auto *detect = new QPushButton(i18n("Add from Window…"), this);
    m_delete = new QPushButton(i18n("Remove"), this);
    // Named as the rest of the settings page names its controls, so that the
    // tests reach them the way they reach everything else on it.
    m_list->setObjectName(QStringLiteral("applicationList"));
    m_name->setObjectName(QStringLiteral("applicationName"));

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
    layout->addLayout(details, 1);

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
    m_name->setEnabled(valid);
    m_enabled->setEnabled(valid);
    m_identity->setEnabled(valid);
    // An entry this build ships comes back with the next package, so removing
    // it would not remove anything. Switching it off is what persists.
    m_delete->setEnabled(valid && !application->shipped);
    if (!valid) {
        m_note->clear();
        return;
    }
    m_name->setText(application->name);
    m_identity->show(*application);
    m_methods->show(application->methods);
    m_settings->show(application->overrides, upscaleGlobalSettings());
    m_enabled->setChecked(application->enabled);
    showNote(*application);
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
    if (m_selecting || m_programQuery) {
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
                QMessageBox::warning(this, i18n("Add from Window"),
                                     i18n("The window could not be identified: %1", reply.error().message()));
            }
            return;
        }
        m_picked = reply.value();
        askForProgram();
    });
}

void UpscaleApplicationEditor::askForProgram()
{
    // KWin's answer names the window but, in 6.3, not its process. The effect
    // can ask KWin for that, so the page asks the effect; without the effect
    // loaded nobody answers, and the window's identity is all there is to go
    // on.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                          QStringLiteral("org.kde.KWin.Effect.Upscale1"),
                                                          QStringLiteral("executablePath"));
    message << m_picked.value(QStringLiteral("uuid")).toString();
    m_programQuery = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(m_programQuery.data(), &QDBusPendingCallWatcher::finished, this, [this]() {
        const QDBusPendingReply<QString> executable = *m_programQuery;
        m_programQuery->deleteLater();
        addIdentified(m_picked, executable.isValid() ? executable.value() : QString());
    });
}

void UpscaleApplicationEditor::addIdentified(const QVariantMap &information, const QString &executable)
{
    const QString windowClass = information.value(QStringLiteral("resourceClass")).toString();
    const QString instance = information.value(QStringLiteral("resourceName")).toString();
    UpscaleApplication application;
    // The exact path names this copy of this program, and it is what a
    // Wayland game is found by before its window exists. A runtime many games
    // share names none of them, and neither does a path that did not resolve;
    // then the window's identity is what names the game.
    if (upscaleIdentifiesOneProgram(executable)) {
        application.executable = executable;
    } else {
        application.windowClass = windowClass;
        application.instance = instance;
    }
    if (application.executable.isEmpty() && application.windowClass.isEmpty() && application.instance.isEmpty()) {
        QMessageBox::warning(this, i18n("Add from Window"), i18n("The window does not identify its application."));
        return;
    }
    application.name = windowClass;
    if (application.name.isEmpty()) {
        application.name = instance.isEmpty() ? executable.section(QLatin1Char('/'), -1) : instance;
    }
    application.id = upscaleNewApplicationId(application.name, m_applications);
    application.order = m_applications.empty() ? 100 : m_applications.back().order + 10;
    m_applications.push_back(application);
    m_original.push_back(UpscaleApplication{});
    rebuildList();
    m_list->setCurrentRow(int(m_applications.size()) - 1);
    m_name->setFocus();
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

bool UpscaleApplicationEditor::save()
{
    // An entry stating no program, window class or instance would match every
    // window on the screen, so the reader drops it; one with a pattern that
    // cannot be used never matches. Writing either would leave an entry that
    // does nothing, and the first would vanish from this list on the next
    // read without saying why.
    const auto unusable = std::ranges::find_if(m_applications, [](const UpscaleApplication &application) {
        return !upscaleIdentityProblem(application).isEmpty();
    });
    if (unusable != m_applications.end()) {
        m_list->setCurrentRow(int(std::ranges::distance(m_applications.begin(), unusable)));
        const bool statesNothing =
            unusable->executable.isEmpty() && unusable->windowClass.isEmpty() && unusable->instance.isEmpty();
        QMessageBox::warning(this, i18n("Applications"),
                             statesNothing ? i18n("“%1” needs a program, window class or instance.", unusable->name)
                                           : i18nc("%1 is an application, %2 what is wrong with its pattern",
                                                   "The pattern in “%1” cannot be used. %2", unusable->name,
                                                   upscaleIdentityProblem(*unusable)));
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
    const auto answer = QMessageBox::question(this, i18n("Restore Defaults"),
                                              i18n("Remove your applications and changes, and restore the default list?"),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    upscaleRestoreApplications();
    load();
    Q_EMIT changed();
}

} // namespace KWin
