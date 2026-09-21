/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale_config.h"

#include "legacysettings.h"
#include "methodcontrols.h"
#include "resolutionchoice.h"
#include "resolutionpreview.h"
#include "settings.h"
#include "sliderfield.h"

#include "application.h"
#include "applicationeditor.h"
#include "placement.h"
#include "resolution.h"
#include "upscaleconfig.h"

// Kept out of the plugin folder, because that folder has to stay a folder KDE
// could copy into KWin unchanged. Out of tree the build adds its include path;
// copied into KWin the header is absent and the page says so.
#if __has_include("buildinfo.h")
#include "buildinfo.h"
#define UPSCALE_BUILD_INFO 1
#else
#define UPSCALE_BUILD_INFO 0
#endif

#include <KAboutData>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

K_PLUGIN_FACTORY(UpscaleEffectConfigFactory, registerPlugin<KWin::UpscaleEffectConfig>();)

namespace KWin
{

UpscaleEffectConfig::UpscaleEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_preset(new QComboBox(widget()))
    , m_percentage(new QSlider(Qt::Horizontal, widget()))
    , m_minimumPixels(new QComboBox(widget()))
    , m_preview(new UpscaleResolutionPreview(this))
    , m_sharpening(new QCheckBox(i18n("Sharpen the image"), widget()))
    , m_strength(new QSlider(Qt::Horizontal, widget()))
    , m_osdDetection(new QCheckBox(i18n("Show info at startup"), widget()))
    , m_osdSummary(new QCheckBox(i18n("Include details"), widget()))
    , m_osdStatistics(new QCheckBox(i18n("Show frame rate"), widget()))
    , m_osdDeveloper(new QCheckBox(i18n("Show developer information"), widget()))
    , m_osdAnnouncementPosition(new QComboBox(widget()))
    , m_osdStatisticsPosition(new QComboBox(widget()))
    , m_osdDeveloperPosition(new QComboBox(widget()))
    , m_osdTimeout(new QSpinBox(widget()))
    , m_build(new QLabel(widget()))
{
    m_preset->setObjectName(QStringLiteral("preset"));
    m_percentage->setObjectName(QStringLiteral("percentage"));
    m_sharpening->setObjectName(QStringLiteral("sharpening"));
    m_strength->setObjectName(QStringLiteral("strength"));
    QTabWidget *all = buildAllPanel();

    // The page: the list with "All applications" first, then what this build
    // is. Grouped the way KWin's own effect pages group theirs, a box per
    // topic titled in title case.
    auto page = new QVBoxLayout(widget());
    const auto section = [this, page](const QString &title) {
        auto box = new QGroupBox(title, widget());
        page->addWidget(box);
        return new QFormLayout(box);
    };
    addApplicationControls(section(i18n("Applications")));
    m_editor->setAllPanel(all);
    // One label column for "All applications" and a game's tabs alike, so
    // that moving between tabs or entries moves no field.
    alignLabels(m_editor->findChildren<QFormLayout *>());
    addAboutControls(section(i18n("About")));
    page->addStretch();
    connectControls();
    // A screen plugged in or unplugged changes which resolutions this system
    // is showing, and the limit offers those first.
    for (const auto signal : {&QGuiApplication::screenAdded, &QGuiApplication::screenRemoved}) {
        connect(qGuiApp, signal, this, [this]() {
            upscaleFillResolutions(m_minimumPixels);
        });
    }
    UpscaleEffectConfig::load();
}

QTabWidget *UpscaleEffectConfig::buildAllPanel()
{
    // The global settings are a profile with no identity, and the page shows
    // them as one: "All applications", the first entry of the list, with the
    // same sections as a game's. Every application follows these values
    // unless its own entry states one, so nothing of it is repeated anywhere
    // else on the page. Whether it acts at all is its check box in the list,
    // as it is for every other entry.
    auto all = new QTabWidget(widget());
    all->setObjectName(QStringLiteral("allApplications"));
    const auto tab = [all](const QString &title) {
        auto contents = new QWidget(all);
        auto form = new QFormLayout(contents);
        all->addTab(contents, title);
        return form;
    };

    QFormLayout *requests = tab(i18n("Resolution Request"));
    addUnlistedControls(requests);

    QFormLayout *resolution = tab(i18n("Resolution"));
    m_preset->addItems({i18n("Native"), i18n("Ultra Quality"), i18n("Quality"), i18n("Balanced"),
                        i18n("Performance"), i18n("Custom")});
    resolution->addRow(i18n("Render resolution:"), m_preset);
    // In basis points, which can name 66.67 %, the share that renders
    // 2560 × 1440 on a 3840 × 2160 screen; see resolutionRatio(). A step is
    // still a whole percent, and the slider snaps to the scales that render a
    // resolution people know on the largest screen.
    m_percentage->setRange(5000, 10000);
    m_percentage->setSingleStep(100);
    m_percentage->setPageStep(1000);
    m_scale = new UpscaleSliderField(m_percentage, widget(), 100);
    m_scale->field()->setObjectName(QStringLiteral("percentageValue"));
    m_scale->field()->setSuffix(i18nc("Suffix: a share of the screen's resolution", "%"));
    resolution->addRow(i18n("Resolution scale:"), m_scale->widget());
    m_preview->build(resolution, widget(), QStringLiteral("preview"));
    addThresholdControl(resolution);

    QFormLayout *sharpening = tab(i18n("Sharpening"));
    sharpening->addRow(QString(), m_sharpening);
    m_strength->setRange(0, 100);
    m_strengthField = new UpscaleSliderField(m_strength, widget(), 1);
    m_strengthField->field()->setObjectName(QStringLiteral("strengthValue"));
    m_strengthField->field()->setSuffix(i18nc("Suffix: a share of the sharpening's full strength", "%"));
    // Zero is a real bypass rather than the weakest setting, so it is named as
    // one instead of being shown as a percentage.
    m_strengthField->field()->setSpecialValueText(i18nc("sharpening strength", "Off"));
    sharpening->addRow(i18n("Strength:"), m_strengthField->widget());

    addDisplayControls(tab(i18n("On-Screen Display")));
    return all;
}

// The version this build calls itself and who wrote it, and nothing else: the
// full build record - branch, revision, build date - is in the log the effect
// writes when KWin loads it and in the developer information on screen, and
// the licence and project address are in the About that System Settings builds
// from the plugin's metadata.
void UpscaleEffectConfig::addAboutControls(QFormLayout *layout)
{
    m_build->setObjectName(QStringLiteral("build"));
    m_build->setTextFormat(Qt::PlainText);
    m_build->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_build->setText(installedVersion());
    layout->addRow(i18n("Version:"), m_build);
    // What does the upscaling. Not a choice, so it is told here rather than
    // offered with the settings.
    layout->addRow(i18n("Scaler:"), new QLabel(i18n("AMD FSR 1"), widget()));
    // Read from the effect's own metadata, which is where the author is
    // maintained, rather than repeated here. This module is built without
    // metadata of its own, so it looks the effect up by its plugin ID the way
    // System Settings does.
    const KPluginMetaData effect = KPluginMetaData::findPluginById(QStringLiteral("kwin/effects/plugins"),
                                                                   QStringLiteral("upscale"));
    QStringList names;
    for (const KAboutPerson &person : effect.authors()) {
        names.append(person.name());
    }
    if (!names.isEmpty()) {
        auto author = new QLabel(names.join(QStringLiteral(", ")), widget());
        author->setObjectName(QStringLiteral("author"));
        author->setTextFormat(Qt::PlainText);
        layout->addRow(names.size() > 1 ? i18n("Authors:") : i18n("Author:"), author);
    }
}

void UpscaleEffectConfig::addThresholdControl(QFormLayout *layout)
{
    m_minimumPixels->setObjectName(QStringLiteral("minimumPixels"));
    // Editable, because the resolutions offered are the ones this system is
    // showing and the ones people usually mean, which is not every resolution
    // anybody might want a threshold at.
    m_minimumPixels->setEditable(true);
    m_minimumPixels->setInsertPolicy(QComboBox::NoInsert);
    m_minimumPixels->setToolTip(i18n("Screens at or below this resolution are left alone. An application can set its own limit."));
    upscaleFillResolutions(m_minimumPixels);
    layout->addRow(i18n("Upscale on screens larger than:"), m_minimumPixels);
    connect(m_minimumPixels, &QComboBox::currentTextChanged, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
}

void UpscaleEffectConfig::connectControls()
{
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_percentage, &QSlider::valueChanged, this, [this]() {
        m_preset->setCurrentIndex(int(ResolutionPreset::Custom));
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_editor, &UpscaleApplicationEditor::allEnabledChanged, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_sharpening, &QCheckBox::toggled, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_strength, &QSlider::valueChanged, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
}

// The application list is a different kind of setting from the rest of this
// page: it is a list the effect ships and the user edits, kept in its own file
// so that a new package can deliver a corrected entry without touching what
// the user changed. Its restore is therefore separate from this page's
// Defaults, which restores the values above and leaves the list alone.
// The global profile's six answers, which unlike every other setting do not
// reach the games in the list: a method is a measurement of one program, so a
// game's unset slot means Automatic rather than this. They are asked of a
// program only while "All applications" is checked, and stay editable while it
// is not, so that they can be set before it is.
void UpscaleEffectConfig::addUnlistedControls(QFormLayout *layout)
{
    auto scope = new QLabel(i18n("For applications not in the list:"), widget());
    layout->addRow(scope);
    // The global profile's own six answers, for a window no profile claimed.
    // Off throughout by default: nothing is known about how an unmeasured
    // program answers, so one asked anything may keep its own resolution or
    // open at the wrong size. Setting one to Automatic is a choice a person
    // makes, not one they inherit.
    m_methods = new UpscaleMethodControls(this);
    m_methods->build(layout, widget());
    connect(m_methods, &UpscaleMethodControls::changed, this, [this]() {
        setNeedsSave(true);
    });
}

// One label column for the whole page, as a single form would have. Each tab
// lays out a form of its own, and left alone each would align its labels to
// its own longest one, so the fields would start at a different place in
// every tab.
void UpscaleEffectConfig::alignLabels(const QList<QFormLayout *> &forms)
{
    QList<QLabel *> labels;
    int widest = 0;
    for (QFormLayout *form : forms) {
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
            if (auto *label = item ? qobject_cast<QLabel *>(item->widget()) : nullptr) {
                // A widened label keeps its text where the style puts a form's
                // labels, which for KDE's is against the field.
                label->setAlignment(form->labelAlignment() | Qt::AlignVCenter);
                labels.append(label);
                widest = std::max(widest, label->sizeHint().width());
            }
        }
    }
    for (QLabel *label : std::as_const(labels)) {
        label->setMinimumWidth(widest);
    }
}

// Everything on the page that follows another control: the scale follows the
// preset, the preview both and the limit, and every game's Global choices
// follow the lot.
//
// Nothing here is greyed out by a switch being off. Every value on this panel
// is a default a game takes when it switches on what the global profile
// leaves off - a game that sharpens takes this strength, one that shows the
// frame rate takes this corner - so each has to stay editable whatever the
// global switches say.
void UpscaleEffectConfig::updatePreview()
{
    const auto preset = static_cast<ResolutionPreset>(m_preset->currentIndex());
    if (preset != ResolutionPreset::Custom) {
        const QSignalBlocker blocker(m_percentage);
        m_percentage->setValue(qRound(resolutionRatio(preset, m_percentage->value()) * 10000));
        m_scale->showSliderValue();
    }
    const UpscaleScreen largest = upscaleLargestScreen();
    m_scale->setSnapPoints(upscaleSnapScales(largest.pixels), m_percentage->singleStep());
    m_percentage->setToolTip(i18n("Share of the screen's width and height the game renders at. Snaps to the "
                                  "common resolutions of %1.",
                                  largest.name));
    m_preview->show(preset, m_percentage->value(), upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()));
    if (m_editor) {
        m_editor->setGlobalSettings(shownSettings());
    }
}

// What a game's Global choices name, which is what "All applications" shows
// rather than what was last applied: a person who changes the preset there
// and then looks at a game expects the game to follow the new one.
UpscaleSettings UpscaleEffectConfig::shownSettings() const
{
    UpscaleSettings settings = upscaleGlobalSettings();
    settings.setActs(m_editor->allEnabled());
    settings.setValue(UpscaleSetting::Resolution, m_preset->currentIndex());
    settings.setValue(UpscaleSetting::Percentage, m_percentage->value());
    settings.setValue(UpscaleSetting::MinimumPixels, upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()));
    settings.setValue(UpscaleSetting::Sharpening, m_sharpening->isChecked());
    settings.setValue(UpscaleSetting::Strength, m_strength->value());
    settings.setValue(UpscaleSetting::OsdDetection, m_osdDetection->isChecked());
    settings.setValue(UpscaleSetting::OsdSummary, m_osdSummary->isChecked());
    settings.setValue(UpscaleSetting::OsdStatistics, m_osdStatistics->isChecked());
    settings.setValue(UpscaleSetting::OsdDeveloper, m_osdDeveloper->isChecked());
    settings.setValue(UpscaleSetting::OsdTimeout, m_osdTimeout->value());
    settings.setValue(UpscaleSetting::AnnouncementPosition, m_osdAnnouncementPosition->currentIndex());
    settings.setValue(UpscaleSetting::StatisticsPosition, m_osdStatisticsPosition->currentIndex());
    settings.setValue(UpscaleSetting::DeveloperPosition, m_osdDeveloperPosition->currentIndex());
    return settings;
}

void UpscaleEffectConfig::showSettings()
{
    // The global profile's own participation, read through the translation of
    // the previous release's key like every other global value on this page.
    const KConfigGroup global(UpscaleConfig::self()->config(), QStringLiteral("Effect-upscale"));
    m_editor->setAllEnabled(UpscaleConfig::unlistedApplications() || upscaleLegacyUnlisted(global));
    m_percentage->setValue(qRound(UpscaleConfig::percentage() * 100));
    m_preset->setCurrentIndex(upscaleSettingInfo(UpscaleSetting::Resolution).global());
    upscaleSelectResolution(m_minimumPixels, UpscaleConfig::minimumPixels());
    m_sharpening->setChecked(UpscaleConfig::sharpening());
    m_strength->setValue(UpscaleConfig::strength());
    m_osdDetection->setChecked(UpscaleConfig::osdDetection());
    m_osdSummary->setChecked(UpscaleConfig::osdSummary());
    m_osdStatistics->setChecked(UpscaleConfig::osdStatistics());
    m_osdDeveloper->setChecked(UpscaleConfig::osdDeveloper());
    // Separated on the way in for the same reason the effect separates them:
    // a file edited by hand can name one corner twice, and the page must not
    // show two displays sharing one.
    std::array<UpscaleCorner, 3> corners{
        upscaleCorner(UpscaleConfig::osdAnnouncementPosition()),
        upscaleCorner(UpscaleConfig::osdStatisticsPosition()),
        upscaleCorner(UpscaleConfig::osdDeveloperPosition()),
    };
    upscaleSeparateCorners(corners);
    const std::array<QComboBox *, 3> positions = positionControls();
    for (std::size_t entry = 0; entry < positions.size(); ++entry) {
        const QSignalBlocker blocker(positions[entry]);
        positions[entry]->setCurrentIndex(int(corners[entry]));
    }
    m_osdTimeout->setValue(UpscaleConfig::osdTimeout());
    m_methods->show(upscaleGlobalMethods());
    updatePreview();
}

void UpscaleEffectConfig::applySettings()
{
    UpscaleConfig::setUnlistedApplications(m_editor->allEnabled());
    UpscaleConfig::setResolution(m_preset->currentIndex());
    UpscaleConfig::setPercentage(m_percentage->value() / 100.0);
    UpscaleConfig::setMinimumPixels(upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()));
    UpscaleConfig::setSharpening(m_sharpening->isChecked());
    UpscaleConfig::setStrength(m_strength->value());
    UpscaleConfig::setOsdDetection(m_osdDetection->isChecked());
    UpscaleConfig::setOsdSummary(m_osdSummary->isChecked());
    UpscaleConfig::setOsdStatistics(m_osdStatistics->isChecked());
    UpscaleConfig::setOsdDeveloper(m_osdDeveloper->isChecked());
    UpscaleConfig::setOsdAnnouncementPosition(m_osdAnnouncementPosition->currentIndex());
    UpscaleConfig::setOsdStatisticsPosition(m_osdStatisticsPosition->currentIndex());
    UpscaleConfig::setOsdDeveloperPosition(m_osdDeveloperPosition->currentIndex());
    UpscaleConfig::setOsdTimeout(m_osdTimeout->value());
    UpscaleMethods methods;
    m_methods->store(methods);
    upscaleSetGlobalMethods(methods);
    // A value equal to the current default is stored as no entry at all, so a
    // build type's default is never written back as if the user chose it.
    UpscaleConfig::self()->save();
    // The new keys now say everything the old ones did, so the old ones go.
    // Only here, on Apply: the effect never rewrites a person's configuration
    // on its own, and until this runs both sides read the same translation.
    KConfigGroup global(UpscaleConfig::self()->config(), QStringLiteral("Effect-upscale"));
    upscaleForgetLegacySettings(global);
    global.sync();
}

void UpscaleEffectConfig::load()
{
    UpscaleConfig::self()->read();
    showSettings();
    // The application list is part of what this page would apply, so Reset
    // discards its pending edits with everything else. Leaving them on screen
    // would let a later Apply write changes the user had just discarded.
    m_editor->load();
    updateApplicationSummary();
    setNeedsSave(false);
}

void UpscaleEffectConfig::defaults()
{
    // The defaults live in upscaleconfig.kcfg. Repeating them here is how the
    // dialog and the effect start to disagree about what "default" means.
    UpscaleConfig::self()->setDefaults();
    showSettings();
    setNeedsSave(true);
}

// The running effect keeps its own copy of the settings and the list.
void UpscaleEffectConfig::reconfigureEffect()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
    message << QStringLiteral("upscale");
    QDBusConnection::sessionBus().asyncCall(message);
}

void UpscaleEffectConfig::save()
{
    // The list can refuse: an entry that could never match a window is kept
    // here rather than written and then silently dropped on the next read.
    // The settings above are applied either way, and the page stays
    // applicable so that the user can correct the entry and press Apply again.
    const bool stored = m_editor->save();
    updateApplicationSummary();
    applySettings();
    setNeedsSave(!stored);
    reconfigureEffect();
}

QString UpscaleEffectConfig::installedVersion()
{
#if UPSCALE_BUILD_INFO
    // Exactly what the version rule of this repository calls the build:
    // "0.1.0" on a release tag, "0.1.0+git<date>.<hash>" with "-dirty" for
    // local changes anywhere else.
    return UpscaleBuildInfo::version();
#else
    return i18n("Unknown");
#endif
}

} // namespace KWin

#include "upscale_config.moc"
