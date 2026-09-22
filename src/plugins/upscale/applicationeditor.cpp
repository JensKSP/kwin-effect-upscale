/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "applicationeditor.h"

#include "matching.h"
#include "resolutionpreview.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <numeric>
#include <ranges>
#include <tuple>
#include <utility>

namespace KWin
{

void UpscaleApplicationEditor::connectControls()
{
    connect(m_list, &QListWidget::currentRowChanged, this, [this]() {
        showSelected();
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_updating) {
            return;
        }
        if (m_list->row(item) == 0) {
            m_allEnabled = item->checkState() == Qt::Checked;
            Q_EMIT allEnabledChanged(m_allEnabled);
            return;
        }
        const int index = m_list->row(item) - rowOf(0);
        if (index >= 0 && size_t(index) < m_applications.size()) {
            m_applications[index].enabled = item->checkState() == Qt::Checked;
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
    , m_preview(new UpscaleResolutionPreview(this))
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
    // The order is the matching order: the first enabled entry that matches
    // wins, so a narrow entry has to come before a broad one it overlaps.
    // Arrows, as KDE's own list editors show them, named for tooltips and
    // screen readers rather than labelled, to keep the row within the list.
    m_up = new QPushButton(QIcon::fromTheme(QStringLiteral("go-up")), QString(), this);
    m_down = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), QString(), this);
    for (const auto &[button, name] : {std::pair{m_up, i18n("Move Up")}, std::pair{m_down, i18n("Move Down")}}) {
        button->setToolTip(name);
        button->setAccessibleName(name);
    }
    // Named as the rest of the settings page names its controls, so that the
    // tests reach them the way they reach everything else on it.
    m_list->setObjectName(QStringLiteral("applicationList"));
    m_name->setObjectName(QStringLiteral("applicationName"));

    m_enabled->setObjectName(QStringLiteral("applicationEnabled"));
    m_note->setObjectName(QStringLiteral("applicationNote"));
    add->setObjectName(QStringLiteral("applicationAdd"));
    detect->setObjectName(QStringLiteral("applicationAddFromWindow"));
    m_delete->setObjectName(QStringLiteral("applicationRemove"));
    m_up->setObjectName(QStringLiteral("applicationMoveUp"));
    m_down->setObjectName(QStringLiteral("applicationMoveDown"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(detect);
    buttons->addWidget(m_delete);
    buttons->addStretch();
    buttons->addWidget(m_up);
    buttons->addWidget(m_down);

    // The list above the details rather than beside them: the details are five
    // tabs and three identity fields that each carry a match type, and beside
    // a list they would not fit a settings page's width.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    layout->addLayout(buttons);
    layout->addLayout(details);

    connectControls();
    connect(add, &QPushButton::clicked, this, &UpscaleApplicationEditor::addApplication);
    connect(detect, &QPushButton::clicked, this, &UpscaleApplicationEditor::addFromWindow);
    connect(m_delete, &QPushButton::clicked, this, &UpscaleApplicationEditor::deleteSelected);
    connect(m_up, &QPushButton::clicked, this, [this]() {
        moveSelected(-1);
    });
    connect(m_down, &QPushButton::clicked, this, [this]() {
        moveSelected(1);
    });
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
    // Checkable like every other row, with the same meaning: whether it acts
    // for the windows it claims, which for the global profile are those no
    // other entry matches. The tooltip says so, because the name alone
    // suggests a switch for everything.
    auto *all = new QListWidgetItem(i18n("All applications"), m_list);
    all->setFlags(all->flags() | Qt::ItemIsUserCheckable);
    all->setCheckState(m_allEnabled ? Qt::Checked : Qt::Unchecked);
    all->setToolTip(i18n("Every application follows these settings unless its own entry sets them. "
                         "Checked, applications that are not in the list are upscaled as well."));
    QFont emphasis = all->font();
    emphasis.setBold(true);
    all->setFont(emphasis);
    for (const UpscaleApplication &application : m_applications) {
        // The name and nothing else. Where an entry came from is answered
        // where it matters - the note under the fields says "Added by you",
        // and Delete is only enabled for such an entry - so decorating every
        // name in the list with it charges the common case for the rare one.
        auto *item = new QListWidgetItem(application.name, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(application.enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_list->setCurrentRow(std::clamp(row, 0, m_list->count() - 1));
    showSelected();
}

int UpscaleApplicationEditor::rowOf(std::size_t index)
{
    return int(index) + 1;
}

bool UpscaleApplicationEditor::allEnabled() const
{
    return m_allEnabled;
}

void UpscaleApplicationEditor::setAllEnabled(bool enabled)
{
    m_allEnabled = enabled;
    const QScopedValueRollback updating(m_updating, true);
    if (QListWidgetItem *all = m_list->item(0)) {
        all->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    }
}

void UpscaleApplicationEditor::setAllPanel(QWidget *panel)
{
    m_details->addWidget(panel);
    showSelected();
}

UpscaleApplication *UpscaleApplicationEditor::selected()
{
    const int index = m_list->currentRow() - rowOf(0);
    return index >= 0 && size_t(index) < m_applications.size() ? &m_applications[index] : nullptr;
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
    m_list->setCurrentRow(rowOf(m_applications.size() - 1));
    m_name->setFocus();
    Q_EMIT changed();
}

void UpscaleApplicationEditor::moveSelected(int step)
{
    const int row = m_list->currentRow() - rowOf(0);
    const int target = row + step;
    if (row < 0 || target < 0 || size_t(row) >= m_applications.size() || size_t(target) >= m_applications.size()) {
        return;
    }
    // The two entries change places and each keeps the Order of the place it
    // takes, so only these two are stored as changed. Where a stored Order
    // already tied or ran backwards, the entries after it are moved on just
    // far enough to keep the list strictly ordered, which is what the reader
    // sorts by.
    std::swap(m_applications[row], m_applications[target]);
    std::swap(m_original[row], m_original[target]);
    std::swap(m_applications[row].order, m_applications[target].order);
    for (size_t index = 1; index < m_applications.size(); ++index) {
        if (m_applications[index].order <= m_applications[index - 1].order) {
            m_applications[index].order = m_applications[index - 1].order + 1;
        }
    }
    rebuildList();
    m_list->setCurrentRow(rowOf(target));
    Q_EMIT changed();
}

void UpscaleApplicationEditor::deleteSelected()
{
    const int row = m_list->currentRow() - rowOf(0);
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
        m_list->setCurrentRow(rowOf(std::size_t(std::ranges::distance(m_applications.begin(), unusable))));
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

bool UpscaleApplicationEditor::exportTo(const QString &path) const
{
    return upscaleWriteApplicationFile(m_applications, path);
}

int UpscaleApplicationEditor::importFrom(const QString &path)
{
    const std::vector<UpscaleApplication> imported = upscaleReadApplicationFile(path);
    for (UpscaleApplication entry : imported) {
        const auto existing = std::ranges::find(m_applications, entry.id, &UpscaleApplication::id);
        if (existing == m_applications.end()) {
            m_applications.push_back(entry);
            m_original.push_back(UpscaleApplication{});
            continue;
        }
        // The file's values replace the entry's; what the package says about
        // it stays, so that saving stores only where the two now differ.
        entry.shipped = existing->shipped;
        if (entry.note.isEmpty()) {
            entry.note = existing->note;
        }
        *existing = entry;
    }
    // In matching order again, the pairs of entry and original kept together.
    std::vector<std::size_t> order(m_applications.size());
    std::ranges::iota(order, std::size_t(0));
    std::ranges::stable_sort(order, [this](std::size_t first, std::size_t second) {
        return std::tie(m_applications[first].order, m_applications[first].id)
            < std::tie(m_applications[second].order, m_applications[second].id);
    });
    std::vector<UpscaleApplication> applications;
    std::vector<UpscaleApplication> originals;
    for (const std::size_t index : order) {
        applications.push_back(m_applications[index]);
        originals.push_back(m_original[index]);
    }
    m_applications = std::move(applications);
    m_original = std::move(originals);
    rebuildList();
    if (!imported.empty()) {
        Q_EMIT changed();
    }
    return int(imported.size());
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
