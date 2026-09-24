/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingcontrols.h"

#include "resolution.h"
#include "resolutionchoice.h"
#include "sliderfield.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <array>

namespace KWin
{

QString upscaleSettingLabel(UpscaleSetting setting)
{
    switch (setting) {
    case UpscaleSetting::Resolution:
        return i18n("Render resolution:");
    case UpscaleSetting::Percentage:
        return i18n("Resolution scale:");
    case UpscaleSetting::MinimumPixels:
        return i18n("Upscale on screens larger than:");
    case UpscaleSetting::Sharpening:
        return i18n("Sharpen the image:");
    case UpscaleSetting::Strength:
        return i18n("Strength:");
    case UpscaleSetting::Osd:
        return i18n("On-screen display:");
    case UpscaleSetting::OsdDetection:
        return i18n("Show info at startup:");
    case UpscaleSetting::OsdSummary:
        return i18n("Include details:");
    case UpscaleSetting::OsdStatistics:
        return i18n("Show frame rate:");
    case UpscaleSetting::OsdDeveloper:
        return i18n("Show developer information:");
    case UpscaleSetting::OsdTimeout:
        return i18n("Show startup info for:");
    case UpscaleSetting::AnnouncementPosition:
        return i18n("Startup info position:");
    case UpscaleSetting::StatisticsPosition:
        return i18n("Frame rate position:");
    case UpscaleSetting::DeveloperPosition:
        return i18n("Developer information position:");
    }
    return QString();
}

// The unit a number is counted in, the same one the settings page shows.
// Appended to the digits, so that a translation can put a space before it.
static QString upscaleSettingSuffix(UpscaleSetting setting, int value)
{
    switch (setting) {
    case UpscaleSetting::Percentage:
        return i18nc("Suffix: a share of the screen's resolution", "%");
    case UpscaleSetting::Strength:
        return i18nc("Suffix: a share of the sharpening's full strength", "%");
    case UpscaleSetting::MinimumPixels:
        return i18ncp("Suffix", " pixel", " pixels", value);
    case UpscaleSetting::OsdTimeout:
        return i18nc("Suffix: the unit symbol for seconds", " s");
    default:
        return QString();
    }
}

int upscaleSettingChoiceCount(UpscaleSetting setting)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    return info.type == UpscaleSettingType::Choice ? info.maximum - info.minimum + 1 : 0;
}

QString upscaleSettingChoiceLabel(UpscaleSetting setting, int value)
{
    if (setting == UpscaleSetting::Resolution) {
        // Indexed by the stored number rather than cast to a preset, as the
        // corners below are, so that a number outside the list names none.
        static constexpr std::array<ResolutionPreset, 6> s_presets{
            ResolutionPreset::Native,
            ResolutionPreset::UltraQuality,
            ResolutionPreset::Quality,
            ResolutionPreset::Balanced,
            ResolutionPreset::Performance,
            ResolutionPreset::Custom,
        };
        if (value < 0 || value >= int(s_presets.size())) {
            return QString();
        }
        switch (s_presets[std::size_t(value)]) {
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

// A switch is a check box that says what it switches, as on the global page,
// in the same words, so that one translation serves both.
static QString upscaleSwitchText(UpscaleSetting setting)
{
    switch (setting) {
    case UpscaleSetting::Sharpening:
        return i18n("Sharpen the image");
    case UpscaleSetting::Osd:
        return i18n("On-screen display");
    case UpscaleSetting::OsdDetection:
        return i18n("Show info at startup");
    case UpscaleSetting::OsdSummary:
        return i18n("Include details");
    case UpscaleSetting::OsdStatistics:
        return i18n("Show frame rate");
    case UpscaleSetting::OsdDeveloper:
        return i18n("Show developer information");
    default:
        return QString();
    }
}

struct UpscaleSettingControls::Control
{
    UpscaleSetting setting;
    // Exactly one kind is used, the kind the global page uses for the same
    // preference, so that a game's page reads as that page does.
    QCheckBox *check = nullptr;
    QComboBox *box = nullptr;
    QComboBox *resolution = nullptr;
    QSpinBox *spin = nullptr;
    QSlider *slider = nullptr;
    UpscaleSliderField *pair = nullptr;
    // What names the preference: the form's label, or a switch's own text.
    QWidget *name = nullptr;
    QToolButton *reset = nullptr;
    // Whether the game states this value. Never inferred from the value
    // itself: a game may state the value the global profile happens to have,
    // and keep it when the global one changes.
    bool stated = false;
};

UpscaleSettingControls::UpscaleSettingControls(QObject *parent)
    : QObject(parent)
{
}

UpscaleSettingControls::~UpscaleSettingControls() = default;

// The value a control shows, which is always the one the entry would use.
static int shownValue(const UpscaleSettingControls::Control &control)
{
    if (control.check) {
        return control.check->isChecked() ? 1 : 0;
    }
    if (control.box) {
        return upscaleSettingInfo(control.setting).minimum + control.box->currentIndex();
    }
    if (control.resolution) {
        return upscaleResolutionPixels(control.resolution, -1);
    }
    return control.spin ? control.spin->value() : control.slider->value();
}

static void showValue(const UpscaleSettingControls::Control &control, int value)
{
    if (control.check) {
        control.check->setChecked(value != 0);
    } else if (control.box) {
        control.box->setCurrentIndex(value - upscaleSettingInfo(control.setting).minimum);
    } else if (control.resolution) {
        // Filled again each time, because the screens it offers first are the
        // ones connected now.
        upscaleFillResolutions(control.resolution);
        upscaleSelectResolution(control.resolution, value);
    } else if (control.spin) {
        control.spin->setValue(value);
    } else {
        control.slider->setValue(value);
        control.pair->showSliderValue();
    }
}

QWidget *UpscaleSettingControls::field(Control &control, QWidget *parent)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(control.setting);
    const UpscaleSetting setting = control.setting;
    const auto edited = [this, setting]() {
        userEdited(setting);
    };
    if (setting == UpscaleSetting::MinimumPixels) {
        // Editable, as on the global page, for a resolution the list does not offer.
        control.resolution = new QComboBox(parent);
        control.resolution->setEditable(true);
        control.resolution->setInsertPolicy(QComboBox::NoInsert);
        control.resolution->setToolTip(i18n("Screens at or below this resolution are left alone."));
        connect(control.resolution, &QComboBox::currentTextChanged, this, edited);
        return control.resolution;
    }
    if (setting == UpscaleSetting::Percentage || setting == UpscaleSetting::Strength) {
        // A slider with its exact value beside it, as on the global page. The
        // scale is held in basis points and shown as the percentage it is.
        control.slider = new QSlider(Qt::Horizontal, parent);
        control.slider->setRange(info.minimum, info.maximum);
        const bool scale = setting == UpscaleSetting::Percentage;
        control.slider->setSingleStep(scale ? 100 : 1);
        control.slider->setPageStep(scale ? 1000 : 10);
        control.pair = new UpscaleSliderField(control.slider, parent, scale ? 100 : 1);
        control.pair->field()->setObjectName(QLatin1String(info.key) + QLatin1String("Value"));
        control.pair->field()->setSuffix(upscaleSettingSuffix(setting, 0));
        if (scale) {
            control.pair->setSnapPoints(upscaleSnapScales(upscaleLargestScreen().pixels), control.slider->singleStep());
        } else {
            control.pair->field()->setSpecialValueText(i18nc("sharpening strength", "Off"));
        }
        control.slider->setObjectName(QLatin1String(info.key));
        connect(control.slider, &QSlider::valueChanged, this, edited);
        return control.pair->widget();
    }
    if (info.type == UpscaleSettingType::Number) {
        control.spin = new QSpinBox(parent);
        control.spin->setRange(info.minimum, info.maximum);
        control.spin->setSuffix(upscaleSettingSuffix(setting, 0));
        connect(control.spin, &QSpinBox::valueChanged, this, edited);
        return control.spin;
    }
    if (info.type == UpscaleSettingType::Switch) {
        control.check = new QCheckBox(upscaleSwitchText(setting), parent);
        connect(control.check, &QCheckBox::toggled, this, edited);
        return control.check;
    }
    control.box = new QComboBox(parent);
    for (int value = info.minimum; value <= info.maximum; ++value) {
        control.box->addItem(upscaleSettingChoiceLabel(setting, value));
    }
    connect(control.box, &QComboBox::currentIndexChanged, this, edited);
    return control.box;
}

void UpscaleSettingControls::build(QFormLayout *form, QWidget *parent, const std::vector<UpscaleSetting> &settings)
{
    for (const UpscaleSetting setting : settings) {
        const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
        m_controls.push_back(Control{setting});
        Control &control = m_controls.back();
        QWidget *value = field(control, parent);
        if (!control.slider) {
            // Named by the key it stores, so that a test or an accessibility
            // tool finds the control without knowing the form's layout.
            value->setObjectName(QLatin1String(info.key));
        }
        // Back to following the global value. At the right end of every row
        // and enabled only where the game states a value, as Qt Designer's
        // reset button is; see mark().
        control.reset = new QToolButton(parent);
        control.reset->setObjectName(QLatin1String(info.key) + QLatin1String("Reset"));
        control.reset->setIcon(QIcon::fromTheme(QStringLiteral("edit-undo")));
        control.reset->setAutoRaise(true);
        control.reset->setToolTip(i18n("Use the value of All applications"));
        control.reset->setAccessibleName(control.reset->toolTip());
        control.reset->setEnabled(false);
        connect(control.reset, &QToolButton::clicked, this, [this, setting]() {
            follow(setting);
        });
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        // The button at the right end of the row, so that every row's lines up
        // in one column, as in Qt Designer.
        layout->addWidget(value, control.slider ? 1 : 0);
        if (!control.slider) {
            layout->addStretch(1);
        }
        layout->addWidget(control.reset);
        if (control.check) {
            form->addRow(QString(), row);
            control.name = control.check;
        } else {
            auto *label = new QLabel(upscaleSettingLabel(setting), parent);
            label->setObjectName(QLatin1String(info.key) + QLatin1String("Name"));
            label->setBuddy(value);
            form->addRow(label, row);
            control.name = label;
        }
    }
}

// What a control shows while it follows the global value. The scale is the
// exception: under any preset but Custom it is that preset's share, as the
// global page shows it, since the global scale applies to Custom alone.
int UpscaleSettingControls::followed(UpscaleSetting setting) const
{
    if (setting == UpscaleSetting::Percentage) {
        const Control *preset = find(UpscaleSetting::Resolution);
        const auto shown = ResolutionPreset(preset ? shownValue(*preset) : m_global.value(UpscaleSetting::Resolution));
        return qRound(resolutionRatio(shown, m_global.value(setting)) * 10000);
    }
    return m_global.value(setting);
}

void UpscaleSettingControls::mark(const Control &control)
{
    QWidget *value = control.check ? static_cast<QWidget *>(control.check) : control.box;
    if (!value) {
        value = control.resolution ? static_cast<QWidget *>(control.resolution) : control.spin;
    }
    if (!value) {
        value = control.pair->field();
    }
    upscaleMarkInherited(control.name, value, control.reset, control.stated);
}

void UpscaleSettingControls::show(const UpscaleSettingOverrides &overrides, const UpscaleSettings &global)
{
    const QScopedValueRollback showing(m_showing, true);
    m_global = global;
    for (Control &control : m_controls) {
        const std::optional<int> &stated = overrides[std::size_t(control.setting)];
        control.stated = stated.has_value();
        showValue(control, stated.value_or(global.value(control.setting)));
    }
    // After the preset, which the followed scale depends on.
    if (Control *scale = controlFor(UpscaleSetting::Percentage); scale && !scale->stated) {
        showValue(*scale, followed(UpscaleSetting::Percentage));
    }
    for (const Control &control : m_controls) {
        mark(control);
    }
}

void UpscaleSettingControls::store(UpscaleSettingOverrides &overrides) const
{
    for (const Control &control : m_controls) {
        std::optional<int> &value = overrides[std::size_t(control.setting)];
        if (!control.stated) {
            value.reset();
            continue;
        }
        // Text in the limit that is no resolution at all keeps what was stated
        // before it was typed.
        const int shown = shownValue(control);
        if (!control.resolution || shown >= 0) {
            value = shown;
        }
    }
}

const UpscaleSettingControls::Control *UpscaleSettingControls::find(UpscaleSetting setting) const
{
    const auto control = std::ranges::find(m_controls, setting, &Control::setting);
    return control != m_controls.end() ? &*control : nullptr;
}

UpscaleSettingControls::Control *UpscaleSettingControls::controlFor(UpscaleSetting setting)
{
    const auto control = std::ranges::find(m_controls, setting, &Control::setting);
    return control != m_controls.end() ? &*control : nullptr;
}

// A person changed a control: the game now states that value. The scale and
// the preset move together as on the global page: stating a scale chooses
// Custom, and any other preset leaves the scale nothing to state, so it goes
// back to following.
void UpscaleSettingControls::userEdited(UpscaleSetting setting)
{
    if (m_showing) {
        return;
    }
    Control *control = controlFor(setting);
    control->stated = true;
    mark(*control);
    Control *preset = controlFor(UpscaleSetting::Resolution);
    Control *scale = controlFor(UpscaleSetting::Percentage);
    if (preset && scale) {
        const QScopedValueRollback showing(m_showing, true);
        const bool custom = shownValue(*preset) == int(ResolutionPreset::Custom);
        if (setting == UpscaleSetting::Percentage && !custom) {
            preset->stated = true;
            showValue(*preset, int(ResolutionPreset::Custom));
            mark(*preset);
        } else if (setting == UpscaleSetting::Resolution && !custom) {
            scale->stated = false;
            showValue(*scale, followed(UpscaleSetting::Percentage));
            mark(*scale);
        }
    }
    Q_EMIT changed();
}

// The reset button: the game stops stating this value and shows the global one.
void UpscaleSettingControls::follow(UpscaleSetting setting)
{
    Control *control = controlFor(setting);
    {
        const QScopedValueRollback showing(m_showing, true);
        control->stated = false;
        showValue(*control, followed(setting));
        mark(*control);
        // A preset that now follows a global one other than Custom leaves the
        // scale nothing to state, as choosing that preset would.
        Control *scale = controlFor(UpscaleSetting::Percentage);
        if (setting == UpscaleSetting::Resolution && scale
            && shownValue(*control) != int(ResolutionPreset::Custom)) {
            scale->stated = false;
            showValue(*scale, followed(UpscaleSetting::Percentage));
            mark(*scale);
        }
    }
    Q_EMIT changed();
}

} // namespace KWin
