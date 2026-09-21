/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "methodcontrols.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QFormLayout>

namespace KWin
{

QString upscalePresentationLabel(UpscalePresentation presentation)
{
    switch (presentation) {
    case UpscalePresentation::WaylandFullScreen:
        return i18n("Wayland, fullscreen:");
    case UpscalePresentation::WaylandBorderless:
        return i18n("Wayland, borderless:");
    case UpscalePresentation::WaylandWindowed:
        return i18n("Wayland, windowed:");
    case UpscalePresentation::X11FullScreen:
        return i18n("X11, fullscreen:");
    case UpscalePresentation::X11Borderless:
        return i18n("X11, borderless:");
    case UpscalePresentation::X11Windowed:
        return i18n("X11, windowed:");
    }
    return QString();
}

QString upscaleMethodLabel(UpscaleMethod method)
{
    switch (method) {
    case UpscaleMethod::Auto:
        return i18n("Automatic");
    case UpscaleMethod::Off:
        return i18n("Ask for nothing");
    case UpscaleMethod::AdvertisedMode:
        return i18n("Advertise a smaller screen mode");
    case UpscaleMethod::AdvertisedScale:
        return i18n("Advertise a smaller screen scale");
    case UpscaleMethod::AdvertisedModeAndScale:
        return i18n("Advertise a smaller mode and scale");
    case UpscaleMethod::X11Resize:
        return i18n("Resize the window");
    }
    return QString();
}

UpscaleMethodControls::UpscaleMethodControls(QObject *parent)
    : QObject(parent)
{
}

void UpscaleMethodControls::build(QFormLayout *form, QWidget *parent)
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
        connect(box, &QComboBox::currentIndexChanged, this, &UpscaleMethodControls::changed);
        m_boxes[slot] = box;
        form->addRow(upscalePresentationLabel(presentation), box);
    }
}

void UpscaleMethodControls::show(const UpscaleMethods &methods)
{
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const auto &offered = m_offered[slot];
        const auto found = std::ranges::find(offered, methods[slot]);
        // A method the slot does not offer is one a later version of the file
        // named. Showing Automatic rather than refusing keeps the entry
        // editable; storage has already read it as asking for nothing.
        m_boxes[slot]->setCurrentIndex(found == offered.end() ? 0 : int(std::ranges::distance(offered.begin(), found)));
    }
}

void UpscaleMethodControls::store(UpscaleMethods &methods) const
{
    for (std::size_t slot = 0; slot < upscalePresentationCount; ++slot) {
        const int index = std::max(0, m_boxes[slot]->currentIndex());
        methods[slot] = m_offered[slot].at(std::size_t(index));
    }
}

} // namespace KWin
