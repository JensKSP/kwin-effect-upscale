/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale_config.h"

#include "legacysettings.h"
#include "methodcontrols.h"
#include "resolutionchoice.h"
#include "settings.h"

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
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include <limits>

K_PLUGIN_FACTORY(UpscaleEffectConfigFactory, registerPlugin<KWin::UpscaleEffectConfig>();)

namespace KWin
{

UpscaleEffectConfig::UpscaleEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_enabled(new QCheckBox(i18n("Upscale unlisted applications"), widget()))
    , m_output(new QComboBox(widget()))
    , m_preset(new QComboBox(widget()))
    , m_percentage(new QSlider(Qt::Horizontal, widget()))
    , m_minimumPixels(new QComboBox(widget()))
    , m_preview(new QLabel(widget()))
    , m_sharpening(new QCheckBox(i18n("Sharpen the image"), widget()))
    , m_strength(new QSlider(Qt::Horizontal, widget()))
    , m_strengthLabel(new QLabel(widget()))
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
    m_enabled->setObjectName(QStringLiteral("enabled"));
    m_preset->setObjectName(QStringLiteral("preset"));
    m_percentage->setObjectName(QStringLiteral("percentage"));
    m_preview->setObjectName(QStringLiteral("preview"));
    m_sharpening->setObjectName(QStringLiteral("sharpening"));
    m_strength->setObjectName(QStringLiteral("strength"));
    // Grouped the way KWin's own effect pages group theirs: a box per topic
    // with a form inside it, and each box titled in title case. What a person
    // sets most comes first; what only some need, and what only informs,
    // comes last.
    auto page = new QVBoxLayout(widget());
    QList<QFormLayout *> forms;
    const auto section = [this, page, &forms](const QString &title) {
        auto box = new QGroupBox(title, widget());
        page->addWidget(box);
        auto form = new QFormLayout(box);
        forms.append(form);
        return form;
    };

    QFormLayout *resolution = section(i18n("Resolution"));
    m_preset->addItems({i18n("Native"), i18n("Ultra Quality"), i18n("Quality"), i18n("Balanced"),
                        i18n("Performance"), i18n("Custom")});
    resolution->addRow(i18n("Render resolution:"), m_preset);
    m_percentage->setRange(50, 100);
    m_percentage->setSingleStep(1);
    resolution->addRow(i18n("Resolution scale:"), m_percentage);
    resolution->addRow(i18n("Screen:"), m_output);
    m_preview->setWordWrap(true);
    resolution->addRow(QString(), m_preview);
    addThresholdControl(resolution);

    QFormLayout *sharpening = section(i18n("Sharpening"));
    sharpening->addRow(QString(), m_sharpening);
    m_strength->setRange(0, 100);
    // The value beside its slider, wide enough for the widest value so that
    // moving the slider does not move the slider.
    m_strengthLabel->setMinimumWidth(m_strengthLabel->fontMetrics().horizontalAdvance(i18nc("sharpening strength", "%1%", 100)));
    auto strength = new QHBoxLayout;
    strength->addWidget(m_strength, 1);
    strength->addWidget(m_strengthLabel);
    sharpening->addRow(i18n("Strength:"), strength);

    // The list spans its box: its rows are the list and the editor beside
    // it, which a label column would only narrow.
    addApplicationControls(section(i18n("Applications")));
    forms.removeLast();
    addUnlistedControls(section(i18n("Unlisted Applications")));
    addDisplayControls(section(i18n("On-Screen Display")));
    addAboutControls(section(i18n("About")));
    alignLabels(forms);
    page->addStretch();
    connectControls();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &UpscaleEffectConfig::updateOutputs);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &UpscaleEffectConfig::updateOutputs);
    updateOutputs();
    UpscaleEffectConfig::load();
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
    connect(m_output, &QComboBox::currentIndexChanged, this, &UpscaleEffectConfig::updatePreview);
    connect(m_preset, &QComboBox::currentIndexChanged, this, [this]() {
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_percentage, &QSlider::valueChanged, this, [this]() {
        m_preset->setCurrentIndex(int(ResolutionPreset::Custom));
        updatePreview();
        setNeedsSave(true);
    });
    connect(m_enabled, &QCheckBox::toggled, this, [this]() {
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
// The switch for the global profile and the six answers it gives, together:
// the answers are asked of a program only while unlisted programs are
// handled, so they follow the switch rather than sitting apart from it.
void UpscaleEffectConfig::addUnlistedControls(QFormLayout *layout)
{
    layout->addRow(QString(), m_enabled);
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

// One label column for the whole page, as a single form would have. Each group
// box lays out a form of its own, and left alone each would align its labels
// to its own longest one, so the fields would start at a different place in
// every box.
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

void UpscaleEffectConfig::addApplicationControls(QFormLayout *layout)
{
    m_editor = new UpscaleApplicationEditor(widget());
    layout->addRow(m_editor);
    connect(m_editor, &UpscaleApplicationEditor::changed, this, [this]() {
        setNeedsSave(true);
        updateApplicationSummary();
    });

    m_applications = new QLabel(widget());
    m_applications->setObjectName(QStringLiteral("applicationSummary"));
    m_applications->setTextFormat(Qt::PlainText);
    m_applications->setWordWrap(true);
    m_resetApplications = new QPushButton(i18n("Restore Defaults"), widget());
    m_resetApplications->setObjectName(QStringLiteral("resetApplications"));
    // What the list is, and the way back to the list the package ships.
    auto status = new QHBoxLayout;
    status->addWidget(m_applications, 1);
    status->addWidget(m_resetApplications);
    layout->addRow(status);
    connect(m_resetApplications, &QPushButton::clicked, this, &UpscaleEffectConfig::resetApplications);
    updateApplicationSummary();
}

void UpscaleEffectConfig::updateApplicationSummary()
{
    const bool customized = UpscaleApplicationEditor::customized();
    m_applications->setText(customized
                                ? i18n("Contains your changes.")
                                : i18n("Default list, updated with each release."));
    m_resetApplications->setEnabled(customized);
}

// The application list is a different kind of setting from the rest of this
// page: it is a list the effect ships and the user edits, kept in its own file
// so that a new package can deliver a corrected entry without touching what
// the user changed. Its restore is therefore separate from this page's
// Defaults, which restores the values above and leaves the list alone.
void UpscaleEffectConfig::resetApplications()
{
    // A different file than Apply writes, and not recoverable afterwards, so
    // the editor asks before doing it and does it at once.
    m_editor->restoreDefaults();
    updateApplicationSummary();
    reconfigureEffect();
}

void UpscaleEffectConfig::updateOutputs()
{
    const QString selected = m_output->currentText();
    m_output->clear();
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        m_output->addItem(screen->name());
        connect(screen, &QScreen::geometryChanged, this, &UpscaleEffectConfig::updatePreview, Qt::UniqueConnection);
        connect(screen, &QScreen::physicalDotsPerInchChanged, this, &UpscaleEffectConfig::updatePreview, Qt::UniqueConnection);
    }
    const int previous = m_output->findText(selected);
    if (previous >= 0) {
        m_output->setCurrentIndex(previous);
    }
    // A screen plugged in or unplugged changes which resolutions this system
    // is showing, and the threshold offers those first.
    upscaleFillResolutions(m_minimumPixels);
    updatePreview();
}

void UpscaleEffectConfig::updatePreview()
{
    const auto screens = QGuiApplication::screens();
    const int index = m_output->currentIndex();
    if (index >= 0 && index < screens.size()) {
        const QScreen *screen = screens[index];
        const QSize pixels = (screen->geometry().size() * screen->devicePixelRatio());
        const auto preset = static_cast<ResolutionPreset>(m_preset->currentIndex());
        const double ratio = resolutionRatio(preset, m_percentage->value());
        const UpscaleSize desired = desiredResolution({pixels.width(), pixels.height()}, preset, m_percentage->value());
        const QSignalBlocker blocker(m_percentage);
        if (preset != ResolutionPreset::Custom) {
            m_percentage->setValue(qRound(ratio * 100));
        }
        m_preview->setText(preset == ResolutionPreset::Native
                               ? i18n("Games render at full resolution and are not upscaled.")
                               : i18nc("render resolution, then its share of the screen", "%2 × %3 (%1%)",
                                       QString::number(ratio * 100, 'f', preset == ResolutionPreset::Custom || preset == ResolutionPreset::Native || preset == ResolutionPreset::Performance ? 0 : 1),
                                       desired.width, desired.height));
        if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()))) {
            m_preview->setText(i18n("Not upscaled: this screen is at or below the resolution limit."));
        }
    }
    m_strength->setEnabled(m_sharpening->isChecked());
    m_strengthLabel->setEnabled(m_sharpening->isChecked());
    if (m_methods) {
        m_methods->setEnabled(m_enabled->isChecked());
    }
    // Zero is a real bypass rather than the weakest setting, so it is named as
    // one instead of being shown as a percentage.
    m_strengthLabel->setText(m_strength->value() == 0 ? i18nc("sharpening strength", "Off")
                                                      : i18nc("sharpening strength", "%1%", m_strength->value()));
    // Each of the four displays is its own switch, so a control is enabled by
    // the display it belongs to and by nothing above it. Turning all four off
    // is what leaves nothing on screen; there is no separate way to say it.
    // A corner is worth choosing only while the display that would occupy it
    // is switched on. Each box follows its own display and nothing else.
    const bool announcing = m_osdDetection->isChecked() || m_osdSummary->isChecked();
    m_osdTimeout->setEnabled(announcing);
    m_osdAnnouncementPosition->setEnabled(announcing);
    m_osdStatisticsPosition->setEnabled(m_osdStatistics->isChecked());
    m_osdDeveloperPosition->setEnabled(m_osdDeveloper->isChecked());
}

void UpscaleEffectConfig::showSettings()
{
    // The global profile's own participation, read through the translation of
    // the previous release's key like every other global value on this page.
    const KConfigGroup global(UpscaleConfig::self()->config(), QStringLiteral("Effect-upscale"));
    m_enabled->setChecked(UpscaleConfig::unlistedApplications() || upscaleLegacyUnlisted(global));
    m_percentage->setValue(UpscaleConfig::percentage());
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
    UpscaleConfig::setUnlistedApplications(m_enabled->isChecked());
    UpscaleConfig::setResolution(m_preset->currentIndex());
    UpscaleConfig::setPercentage(m_percentage->value());
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
