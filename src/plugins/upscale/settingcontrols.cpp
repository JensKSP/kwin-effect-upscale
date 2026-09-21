/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingcontrols.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QFormLayout>
#include <QSpinBox>

#include <array>

namespace KWin
{

QString upscaleSettingLabel(UpscaleSetting setting)
{
    switch (setting) {
    case UpscaleSetting::Resolution:
        return i18n("Resolution:");
    case UpscaleSetting::Percentage:
        return i18n("Custom resolution:");
    case UpscaleSetting::MinimumPixels:
        return i18n("Biggest output not to scale, in pixels:");
    case UpscaleSetting::Sharpening:
        return i18n("Sharpening:");
    case UpscaleSetting::Strength:
        return i18n("Sharpening strength:");
    case UpscaleSetting::ResolutionControl:
        return i18n("Ask the application to render smaller:");
    case UpscaleSetting::Osd:
        return i18n("On-screen display:");
    case UpscaleSetting::OsdDetection:
        return i18n("Announce this application:");
    case UpscaleSetting::OsdSummary:
        return i18n("Include a summary:");
    case UpscaleSetting::OsdStatistics:
        return i18n("Show the frame rate:");
    case UpscaleSetting::OsdDeveloper:
        return i18n("Developer information:");
    case UpscaleSetting::OsdTimeout:
        return i18n("Announcement timeout:");
    case UpscaleSetting::AnnouncementPosition:
        return i18n("Announcement position:");
    case UpscaleSetting::StatisticsPosition:
        return i18n("Frame rate position:");
    case UpscaleSetting::DeveloperPosition:
        return i18n("Developer information position:");
    }
    return QString();
}

int upscaleSettingChoiceCount(UpscaleSetting setting)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    return info.type == UpscaleSettingType::Choice ? info.maximum - info.minimum + 1 : 0;
}

QString upscaleSettingChoiceLabel(UpscaleSetting setting, int value)
{
    if (setting == UpscaleSetting::Resolution) {
        if (value < 0 || value > int(ResolutionPreset::Custom)) {
            return QString();
        }
        switch (ResolutionPreset(value)) {
        case ResolutionPreset::Native:
            // Named for what it does rather than for what it is called
            // elsewhere: a person choosing it is choosing to be left alone.
            return i18n("Native — do not reduce");
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
    // The three position preferences share one list, in the order stored. The
    // stored number indexes it rather than being cast to a corner, so that a
    // number outside it names no corner instead of one that does not exist.
    static constexpr std::array<UpscaleCorner, 4> s_corners{
        UpscaleCorner::TopLeft,
        UpscaleCorner::TopRight,
        UpscaleCorner::BottomLeft,
        UpscaleCorner::BottomRight,
    };
    if (value < 0 || value >= int(s_corners.size())) {
        return QString();
    }
    switch (s_corners[std::size_t(value)]) {
    case UpscaleCorner::TopLeft:
        return i18n("Top left");
    case UpscaleCorner::TopRight:
        return i18n("Top right");
    case UpscaleCorner::BottomLeft:
        return i18n("Bottom left");
    case UpscaleCorner::BottomRight:
        return i18n("Bottom right");
    }
    return QString();
}

struct UpscaleSettingControls::Control
{
    UpscaleSetting setting;
    // Exactly one of these is used, decided by the row's type. A switch and a
    // choice are both lists with "use global" first; a number reserves the
    // step below its range for the same answer, which is what a spin box can
    // give a name to.
    QComboBox *box = nullptr;
    QSpinBox *spin = nullptr;
};

UpscaleSettingControls::UpscaleSettingControls(QObject *parent)
    : QObject(parent)
{
}

UpscaleSettingControls::~UpscaleSettingControls() = default;

void UpscaleSettingControls::build(QFormLayout *form, QWidget *parent, const std::vector<UpscaleSetting> &settings)
{
    for (const UpscaleSetting setting : settings) {
        const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
        Control control{setting};
        if (info.type == UpscaleSettingType::Number) {
            control.spin = new QSpinBox(parent);
            // One step below the range means "follow the global value", and
            // the spin box gives that step a name. The range itself is the
            // table's, so a control can never offer a value storage would
            // clamp away.
            control.spin->setRange(info.minimum - 1, info.maximum);
            connect(control.spin, &QSpinBox::valueChanged, this, &UpscaleSettingControls::changed);
            form->addRow(upscaleSettingLabel(setting), control.spin);
        } else {
            control.box = new QComboBox(parent);
            connect(control.box, &QComboBox::currentIndexChanged, this, &UpscaleSettingControls::changed);
            form->addRow(upscaleSettingLabel(setting), control.box);
        }
        // Named by the key it stores, so that a test or an accessibility tool
        // finds the control for a preference without knowing the form's layout.
        QWidget *widget = control.box ? static_cast<QWidget *>(control.box) : control.spin;
        widget->setObjectName(QLatin1String(info.key));
        m_controls.push_back(control);
    }
}

void UpscaleSettingControls::show(const UpscaleSettingOverrides &overrides, const UpscaleSettings &global)
{
    for (const Control &control : m_controls) {
        const UpscaleSettingInfo &info = upscaleSettingInfo(control.setting);
        const std::optional<int> &stated = overrides[std::size_t(control.setting)];
        const int inherited = global.value(control.setting);
        if (control.spin) {
            // Naming the inherited value in the special text is the whole
            // point: "use global" on its own tells a person nothing about
            // what they would get.
            control.spin->setSpecialValueText(i18n("Use global (%1)", inherited));
            control.spin->setValue(stated ? *stated : info.minimum - 1);
            continue;
        }
        control.box->clear();
        if (info.type == UpscaleSettingType::Switch) {
            control.box->addItem(inherited ? i18n("Use global (on)") : i18n("Use global (off)"));
            control.box->addItem(i18n("On"));
            control.box->addItem(i18n("Off"));
            // Use global, On, Off, in that order.
            int index = 0;
            if (stated) {
                index = *stated ? 1 : 2;
            }
            control.box->setCurrentIndex(index);
            continue;
        }
        control.box->addItem(i18n("Use global (%1)", upscaleSettingChoiceLabel(control.setting, inherited)));
        for (int value = info.minimum; value <= info.maximum; ++value) {
            control.box->addItem(upscaleSettingChoiceLabel(control.setting, value));
        }
        control.box->setCurrentIndex(stated ? *stated - info.minimum + 1 : 0);
    }
}

void UpscaleSettingControls::store(UpscaleSettingOverrides &overrides) const
{
    for (const Control &control : m_controls) {
        const UpscaleSettingInfo &info = upscaleSettingInfo(control.setting);
        std::optional<int> &stated = overrides[std::size_t(control.setting)];
        if (control.spin) {
            const int value = control.spin->value();
            stated = value < info.minimum ? std::nullopt : std::optional<int>(value);
            continue;
        }
        const int index = control.box->currentIndex();
        if (index <= 0) {
            stated = std::nullopt;
        } else if (info.type == UpscaleSettingType::Switch) {
            stated = index == 1 ? 1 : 0;
        } else {
            stated = info.minimum + index - 1;
        }
    }
}

bool UpscaleSettingControls::anyStated() const
{
    return std::ranges::any_of(m_controls, [](const Control &control) {
        return control.spin ? control.spin->value() >= upscaleSettingInfo(control.setting).minimum
                            : control.box->currentIndex() > 0;
    });
}

void UpscaleSettingControls::clear()
{
    for (const Control &control : m_controls) {
        if (control.spin) {
            control.spin->setValue(upscaleSettingInfo(control.setting).minimum - 1);
        } else {
            control.box->setCurrentIndex(0);
        }
    }
}

} // namespace KWin
