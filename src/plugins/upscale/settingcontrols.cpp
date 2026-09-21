/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingcontrols.h"

#include "resolutionchoice.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSpinBox>

#include <algorithm>
#include <array>
#include <type_traits>

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
        return i18ncp("Suffix", " second", " seconds", value);
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

namespace
{

// A number that may follow the global value. Its lowest step stands for
// "Global", and stepping off it starts from the value it was following rather
// than from the bottom of the range: a person raising the scale of a game
// that follows 67% expects to go on from 67%, not to be dropped to 50%.
template<typename Box, typename Value>
class GlobalBox : public Box
{
public:
    using Box::Box;

    void setInherited(Value inherited)
    {
        m_inherited = inherited;
    }

    void stepBy(int steps) override
    {
        if (this->value() == this->minimum() && steps > 0) {
            // The first step states the followed value itself, so that what
            // the field shows only stops being "Global".
            this->setValue(std::clamp<Value>(m_inherited + steps - 1, this->minimum() + s_smallest, this->maximum()));
            return;
        }
        Box::stepBy(steps);
    }

private:
    static constexpr Value s_smallest = std::is_integral_v<Value> ? Value(1) : Value(0.01);
    Value m_inherited = 0;
};

using GlobalSpinBox = GlobalBox<QSpinBox, int>;
// The scale, held in basis points and shown as the percentage it is, to the
// hundredth that names 66.67 %; see resolutionRatio().
using GlobalShareBox = GlobalBox<QDoubleSpinBox, double>;

} // namespace

struct UpscaleSettingControls::Control
{
    UpscaleSetting setting;
    // Exactly one of these is used, decided by the row. A switch and a choice
    // are both lists with "use global" first; a number reserves the step below
    // its range for the same answer, which is what a spin box can give a name
    // to. The scale is such a number with two decimals. The resolution limit
    // is a number nobody thinks of as one, so it is offered as the global page
    // offers it, as resolutions, with "use global" first like every list here.
    QComboBox *box = nullptr;
    GlobalSpinBox *spin = nullptr;
    GlobalShareBox *share = nullptr;
    QComboBox *resolution = nullptr;
};

UpscaleSettingControls::UpscaleSettingControls(QObject *parent)
    : QObject(parent)
{
}

UpscaleSettingControls::~UpscaleSettingControls() = default;

// Editable, as on the global page, for a resolution the list does not offer.
static QComboBox *resolutionControl(QWidget *parent, UpscaleSettingControls *owner)
{
    auto *box = new QComboBox(parent);
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    box->setToolTip(i18n("Screens at or below this resolution are left alone."));
    QObject::connect(box, &QComboBox::currentTextChanged, owner, &UpscaleSettingControls::changed);
    return box;
}

// One step below the range means "follow the global value", and the box gives
// that step a name. The range itself is the table's, so a control can never
// offer a value storage would clamp away.
static GlobalShareBox *shareControl(QWidget *parent, UpscaleSettingControls *owner)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(UpscaleSetting::Percentage);
    auto *share = new GlobalShareBox(parent);
    share->setDecimals(2);
    share->setRange((info.minimum - 1) / 100.0, info.maximum / 100.0);
    share->setSingleStep(1);
    share->setSuffix(upscaleSettingSuffix(UpscaleSetting::Percentage, 0));
    QObject::connect(share, &QDoubleSpinBox::valueChanged, owner, &UpscaleSettingControls::changed);
    return share;
}

static GlobalSpinBox *numberControl(UpscaleSetting setting, QWidget *parent, UpscaleSettingControls *owner)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    auto *spin = new GlobalSpinBox(parent);
    spin->setRange(info.minimum - 1, info.maximum);
    // The plural of a unit depends on the number, so it follows it.
    const auto unit = [spin, setting]() {
        spin->setSuffix(upscaleSettingSuffix(setting, spin->value()));
    };
    unit();
    QObject::connect(spin, &QSpinBox::valueChanged, spin, unit);
    QObject::connect(spin, &QSpinBox::valueChanged, owner, &UpscaleSettingControls::changed);
    return spin;
}

void UpscaleSettingControls::build(QFormLayout *form, QWidget *parent, const std::vector<UpscaleSetting> &settings)
{
    for (const UpscaleSetting setting : settings) {
        const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
        Control control{setting};
        QWidget *widget = nullptr;
        if (setting == UpscaleSetting::MinimumPixels) {
            widget = control.resolution = resolutionControl(parent, this);
        } else if (setting == UpscaleSetting::Percentage) {
            widget = control.share = shareControl(parent, this);
        } else if (info.type == UpscaleSettingType::Number) {
            widget = control.spin = numberControl(setting, parent, this);
        } else {
            widget = control.box = new QComboBox(parent);
            connect(control.box, &QComboBox::currentIndexChanged, this, &UpscaleSettingControls::changed);
        }
        form->addRow(upscaleSettingLabel(setting), widget);
        // Named by the key it stores, so that a test or an accessibility tool
        // finds the control for a preference without knowing the form's layout.
        widget->setObjectName(QLatin1String(info.key));
        m_controls.push_back(control);
        if (setting == UpscaleSetting::Percentage) {
            coupleScaleToPreset();
        }
    }
}

// The first choice of every inheritable control. KDE's own pattern for
// following something else is "Default (Breeze)", but Default already names
// the page's Restore Defaults, which means something different.
static QString globalChoice(const QString &inherited)
{
    return i18nc("A profile's setting that follows the global value, named in brackets", "Global (%1)", inherited);
}

// The limit, as a list of resolutions with the followed one first. Filled
// again each time, because the screens it offers first are the ones
// connected now.
static void showResolution(QComboBox *box, const std::optional<int> &stated, int inherited)
{
    upscaleFillResolutions(box);
    box->insertItem(0, globalChoice(upscaleResolutionName(inherited)), -1);
    box->setCurrentIndex(-1);
    if (stated) {
        upscaleSelectResolution(box, *stated);
    } else {
        box->setCurrentIndex(0);
    }
}

// Naming the inherited value in the special text is the whole point: "Global"
// on its own tells a person nothing about what they would get.
// A percentage to the hundredth it needs and no further: 66.67 %, 75 %.
static QString percentText(int basisPoints)
{
    const QLocale locale;
    QString text = locale.toString(basisPoints / 100.0, 'f', 2);
    const QString point = locale.decimalPoint();
    if (text.contains(point)) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(point)) {
            text.chop(point.size());
        }
    }
    return text;
}

static void showShare(GlobalShareBox *share, const std::optional<int> &stated, int inherited)
{
    share->setSpecialValueText(globalChoice(percentText(inherited) + upscaleSettingSuffix(UpscaleSetting::Percentage, 0)));
    share->setInherited(inherited / 100.0);
    share->setValue(stated ? *stated / 100.0 : share->minimum());
}

static void showNumber(GlobalSpinBox *spin, UpscaleSetting setting, const std::optional<int> &stated, int inherited)
{
    spin->setSpecialValueText(globalChoice(QString::number(inherited) + upscaleSettingSuffix(setting, inherited)));
    spin->setInherited(inherited);
    spin->setValue(stated ? *stated : upscaleSettingInfo(setting).minimum - 1);
}

// A switch is Global, On, Off, in that order; a choice is Global and then its
// values in the order stored.
static void showList(QComboBox *box, UpscaleSetting setting, const std::optional<int> &stated, int inherited)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(setting);
    box->clear();
    if (info.type == UpscaleSettingType::Switch) {
        box->addItems({globalChoice(inherited ? i18n("On") : i18n("Off")), i18n("On"), i18n("Off")});
        int index = 0;
        if (stated) {
            index = *stated ? 1 : 2;
        }
        box->setCurrentIndex(index);
        return;
    }
    box->addItem(globalChoice(upscaleSettingChoiceLabel(setting, inherited)));
    for (int value = info.minimum; value <= info.maximum; ++value) {
        box->addItem(upscaleSettingChoiceLabel(setting, value));
    }
    box->setCurrentIndex(stated ? *stated - info.minimum + 1 : 0);
}

void UpscaleSettingControls::show(const UpscaleSettingOverrides &overrides, const UpscaleSettings &global)
{
    const QScopedValueRollback showing(m_showing, true);
    m_global = global;
    for (const Control &control : m_controls) {
        const std::optional<int> &stated = overrides[std::size_t(control.setting)];
        const int inherited = global.value(control.setting);
        if (control.resolution) {
            showResolution(control.resolution, stated, inherited);
        } else if (control.share) {
            showShare(control.share, stated, inherited);
        } else if (control.spin) {
            showNumber(control.spin, control.setting, stated, inherited);
        } else {
            showList(control.box, control.setting, stated, inherited);
        }
    }
}

std::optional<int> UpscaleSettingControls::stated(const Control &control, std::optional<int> fallback)
{
    const UpscaleSettingInfo &info = upscaleSettingInfo(control.setting);
    if (control.resolution) {
        // "Global" carries -1, and text that is no resolution at all keeps
        // what was stated before it was typed.
        const int pixels = upscaleResolutionPixels(control.resolution, fallback.value_or(-1));
        return pixels < 0 ? std::nullopt : std::optional<int>(pixels);
    }
    if (control.share) {
        const int value = qRound(control.share->value() * 100);
        return value < info.minimum ? std::nullopt : std::optional<int>(value);
    }
    if (control.spin) {
        const int value = control.spin->value();
        return value < info.minimum ? std::nullopt : std::optional<int>(value);
    }
    const int index = control.box->currentIndex();
    if (index <= 0) {
        return std::nullopt;
    }
    if (info.type == UpscaleSettingType::Switch) {
        return index == 1 ? 1 : 0;
    }
    return info.minimum + index - 1;
}

void UpscaleSettingControls::store(UpscaleSettingOverrides &overrides) const
{
    for (const Control &control : m_controls) {
        std::optional<int> &value = overrides[std::size_t(control.setting)];
        value = stated(control, value);
    }
}

const UpscaleSettingControls::Control *UpscaleSettingControls::find(UpscaleSetting setting) const
{
    const auto control = std::ranges::find(m_controls, setting, &Control::setting);
    return control != m_controls.end() ? &*control : nullptr;
}

int UpscaleSettingControls::effective(UpscaleSetting setting) const
{
    const Control *control = find(setting);
    const std::optional<int> value = control ? stated(*control, std::nullopt) : std::nullopt;
    return value.value_or(m_global.value(setting));
}

// On the global page, moving the scale chooses Custom, because the scale is
// what Custom is and means nothing to any other preset; choosing a preset
// moves the scale to its ratio. A game's entry does the same with a Global
// choice added: stating a scale states Custom, unless Custom is what the entry
// already follows, and choosing any other preset puts the scale back to
// following the global value, since a scale it states would no longer apply.
void UpscaleSettingControls::coupleScaleToPreset()
{
    const Control *preset = find(UpscaleSetting::Resolution);
    const Control *scale = find(UpscaleSetting::Percentage);
    if (!preset || !preset->box || !scale || !scale->share) {
        return;
    }
    QComboBox *box = preset->box;
    QDoubleSpinBox *spin = scale->share;
    const int custom = int(ResolutionPreset::Custom) - upscaleSettingInfo(UpscaleSetting::Resolution).minimum + 1;
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, box, spin, custom]() {
        if (m_showing || spin->value() == spin->minimum()) {
            return;
        }
        if (effective(UpscaleSetting::Resolution) != int(ResolutionPreset::Custom)) {
            box->setCurrentIndex(custom);
        }
    });
    connect(box, &QComboBox::currentIndexChanged, this, [this, spin]() {
        if (!m_showing && effective(UpscaleSetting::Resolution) != int(ResolutionPreset::Custom)) {
            spin->setValue(spin->minimum());
        }
    });
}

} // namespace KWin
