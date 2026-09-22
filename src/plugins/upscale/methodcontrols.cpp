/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "methodcontrols.h"

#include "settingcontrols.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScopedValueRollback>
#include <QToolButton>

#include <algorithm>

namespace KWin
{

QString upscalePresentationLabel(UpscalePresentation presentation)
{
    switch (presentation) {
    case UpscalePresentation::WaylandFullScreen:
        return i18n("Wayland fullscreen:");
    case UpscalePresentation::WaylandBorderless:
        return i18n("Wayland borderless:");
    case UpscalePresentation::WaylandWindowed:
        return i18n("Wayland windowed:");
    case UpscalePresentation::X11FullScreen:
        return i18n("X11 fullscreen:");
    case UpscalePresentation::X11Borderless:
        return i18n("X11 borderless:");
    case UpscalePresentation::X11Windowed:
        return i18n("X11 windowed:");
    }
    return QString();
}

QString upscaleMethodLabel(UpscaleMethod method)
{
    switch (method) {
    case UpscaleMethod::Auto:
        return i18n("Automatic");
    case UpscaleMethod::Off:
        return i18nc("A resolution request method", "Off");
    case UpscaleMethod::AdvertisedMode:
        return i18n("Smaller screen mode");
    case UpscaleMethod::AdvertisedScale:
        return i18n("Smaller screen scale");
    case UpscaleMethod::AdvertisedModeAndScale:
        return i18n("Smaller mode and scale");
    case UpscaleMethod::X11Resize:
        return i18n("Smaller X11 buffer");
    }
    return QString();
}

UpscaleMethodControls::UpscaleMethodControls(QObject *parent)
    : QObject(parent)
{
}

void UpscaleMethodControls::build(QFormLayout *form, QWidget *parent, bool inherit)
{
    static constexpr std::array<UpscaleMethod, 6> s_everyMethod{
        UpscaleMethod::Auto,
        UpscaleMethod::AdvertisedMode,
        UpscaleMethod::AdvertisedScale,
        UpscaleMethod::AdvertisedModeAndScale,
        UpscaleMethod::X11Resize,
        UpscaleMethod::Off,
    };
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const auto presentation = UpscalePresentation(slot);
        auto *box = new QComboBox(parent);
        for (const UpscaleMethod method : s_everyMethod) {
            if (upscaleMethodApplies(presentation, method)) {
                m_offered[slot].push_back(method);
                box->addItem(upscaleMethodLabel(method));
            }
        }
        box->setObjectName(QLatin1String("method") + QString::number(slot));
        m_boxes[slot] = box;
        if (!inherit) {
            connect(box, &QComboBox::currentIndexChanged, this, &UpscaleMethodControls::changed);
            form->addRow(upscalePresentationLabel(presentation), box);
            continue;
        }
        connect(box, &QComboBox::currentIndexChanged, this, [this, slot]() {
            edited(slot);
        });
        // A game's row as a game's other settings lay theirs out: the list,
        // and the reset button at the right end of the row.
        m_names[slot] = new QLabel(upscalePresentationLabel(presentation), parent);
        m_names[slot]->setObjectName(QLatin1String("method") + QString::number(slot) + QLatin1String("Name"));
        m_names[slot]->setBuddy(box);
        m_resets[slot] = new QToolButton(parent);
        m_resets[slot]->setObjectName(QLatin1String("method") + QString::number(slot) + QLatin1String("Reset"));
        m_resets[slot]->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
        m_resets[slot]->setAutoRaise(true);
        m_resets[slot]->setToolTip(i18n("Use the measured method, or the one of All applications"));
        m_resets[slot]->setAccessibleName(m_resets[slot]->toolTip());
        m_resets[slot]->setEnabled(false);
        connect(m_resets[slot], &QToolButton::clicked, this, [this, slot]() {
            follow(slot);
        });
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(box);
        layout->addStretch(1);
        layout->addWidget(m_resets[slot]);
        form->addRow(m_names[slot], row);
    }
    // One width for the six, so they read as the one table they are rather
    // than as boxes as wide as each one's longest entry.
    int widest = 0;
    for (const QComboBox *box : m_boxes) {
        widest = std::max(widest, box->sizeHint().width());
    }
    for (QComboBox *box : m_boxes) {
        box->setMinimumWidth(widest);
    }
}

void UpscaleMethodControls::setEnabled(bool enabled)
{
    for (QComboBox *box : m_boxes) {
        box->setEnabled(enabled);
    }
}

void UpscaleMethodControls::select(std::size_t slot, UpscaleMethod method)
{
    const auto &offered = m_offered[slot];
    const auto found = std::ranges::find(offered, method);
    // A method the slot does not offer is one a later version of the file
    // named. Showing Automatic rather than refusing keeps the entry editable;
    // storage has already read it as asking for nothing.
    m_boxes[slot]->setCurrentIndex(found == offered.end() ? 0 : int(std::ranges::distance(offered.begin(), found)));
}

void UpscaleMethodControls::show(const UpscaleMethods &methods)
{
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        select(slot, methods[slot]);
    }
}

void UpscaleMethodControls::store(UpscaleMethods &methods) const
{
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const int index = std::max(0, m_boxes[slot]->currentIndex());
        methods[slot] = m_offered[slot].at(std::size_t(index));
    }
}

void UpscaleMethodControls::show(const UpscaleStatedMethods &methods, const UpscaleStatedMethods &measured,
                                 const UpscaleMethods &global)
{
    const QScopedValueRollback showing(m_showing, true);
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        // A slot the package measured follows that measurement; any other
        // follows the global profile. Stating what the parent states is
        // following it.
        m_parents[slot] = measured[slot].value_or(global[slot]);
        m_own[slot] = methods[slot].has_value() && methods[slot] != measured[slot];
        select(slot, m_own[slot] ? *methods[slot] : m_parents[slot]);
        mark(slot);
    }
}

void UpscaleMethodControls::store(UpscaleStatedMethods &methods, const UpscaleStatedMethods &measured) const
{
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        if (m_own[slot]) {
            const int index = std::max(0, m_boxes[slot]->currentIndex());
            methods[slot] = m_offered[slot].at(std::size_t(index));
        } else {
            methods[slot] = measured[slot];
        }
    }
}

// The rule a game's settings follow, here for its methods; see
// UpscaleSettingControls::mark().
void UpscaleMethodControls::mark(std::size_t slot)
{
    upscaleMarkInherited(m_names[slot], m_boxes[slot], m_resets[slot], m_own[slot]);
}

void UpscaleMethodControls::edited(std::size_t slot)
{
    if (m_showing) {
        return;
    }
    m_own[slot] = true;
    mark(slot);
    Q_EMIT changed();
}

void UpscaleMethodControls::follow(std::size_t slot)
{
    {
        const QScopedValueRollback showing(m_showing, true);
        m_own[slot] = false;
        select(slot, m_parents[slot]);
        mark(slot);
    }
    Q_EMIT changed();
}

} // namespace KWin
