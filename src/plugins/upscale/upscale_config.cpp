/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale_config.h"

#include "resolutionchoice.h"

#include "application.h"
#include "applicationeditor.h"
#include "placement.h"
#include "resolution.h"
#include "supportinformation.h"
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

#include <KLocalizedString>
#include <KPluginFactory>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

#include <limits>

K_PLUGIN_FACTORY(UpscaleEffectConfigFactory, registerPlugin<KWin::UpscaleEffectConfig>();)

namespace KWin
{

UpscaleEffectConfig::UpscaleEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_enabled(new QCheckBox(i18n("Enable upscaling"), widget()))
    , m_output(new QComboBox(widget()))
    , m_preset(new QComboBox(widget()))
    , m_percentage(new QSlider(Qt::Horizontal, widget()))
    , m_minimumPixels(new QComboBox(widget()))
    , m_preview(new QLabel(widget()))
    , m_sharpening(new QCheckBox(i18n("Enable RCAS sharpening"), widget()))
    , m_strength(new QSlider(Qt::Horizontal, widget()))
    , m_strengthLabel(new QLabel(widget()))
    , m_osd(new QCheckBox(i18n("Show the on-screen display"), widget()))
    , m_osdDetection(new QCheckBox(i18n("Announce the selected application"), widget()))
    , m_osdSummary(new QCheckBox(i18n("Include a short summary in the announcement"), widget()))
    , m_osdStatistics(new QCheckBox(i18n("Show the frame rate on screen"), widget()))
    , m_osdDeveloper(new QCheckBox(i18n("Add developer information"), widget()))
    , m_osdPosition(new QComboBox(widget()))
    , m_osdTimeout(new QSpinBox(widget()))
    , m_build(new QLabel(widget()))
    , m_status(new QLabel(widget()))
{
    m_enabled->setObjectName(QStringLiteral("enabled"));
    m_preset->setObjectName(QStringLiteral("preset"));
    m_percentage->setObjectName(QStringLiteral("percentage"));
    m_preview->setObjectName(QStringLiteral("preview"));
    m_sharpening->setObjectName(QStringLiteral("sharpening"));
    m_strength->setObjectName(QStringLiteral("strength"));
    auto layout = new QFormLayout(widget());
    layout->addRow(m_enabled);
    layout->addRow(i18n("Scaler:"), new QLabel(i18n("FSR 1 (EASU)"), widget()));
    layout->addRow(i18n("Preview output:"), m_output);
    m_preset->addItems({i18n("Automatic"), i18n("Native"), i18n("Ultra Quality"), i18n("Quality"),
                        i18n("Balanced"), i18n("Performance"), i18n("Custom")});
    layout->addRow(i18n("Preferred game resolution:"), m_preset);
    m_percentage->setRange(50, 100);
    m_percentage->setSingleStep(1);
    layout->addRow(i18n("Input size:"), m_percentage);
    m_preview->setWordWrap(true);
    layout->addRow(m_preview);
    addThresholdControl(layout);
    layout->addRow(m_sharpening);
    m_strength->setRange(0, 100);
    layout->addRow(i18n("Sharpening strength:"), m_strength);
    layout->addRow(m_strengthLabel);
    addDisplayControls(layout);
    addApplicationControls(layout);
    addStatusControls(layout);
    connectControls();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &UpscaleEffectConfig::updateOutputs);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &UpscaleEffectConfig::updateOutputs);
    updateOutputs();
    UpscaleEffectConfig::load();
}

void UpscaleEffectConfig::addStatusControls(QFormLayout *layout)
{
    m_build->setObjectName(QStringLiteral("build"));
    m_build->setWordWrap(true);
    m_build->setTextFormat(Qt::PlainText);
    m_build->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_build->setText(installedBuild());
    layout->addRow(i18n("Version:"), m_build);
    m_status->setObjectName(QStringLiteral("status"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addRow(m_status);
    auto refresh = new QPushButton(i18n("Refresh supplied-buffer status"), widget());
    refresh->setObjectName(QStringLiteral("refreshStatus"));
    layout->addRow(refresh);
    connect(refresh, &QPushButton::clicked, this, &UpscaleEffectConfig::refreshStatus);
}

void UpscaleEffectConfig::addThresholdControl(QFormLayout *layout)
{
    m_minimumPixels->setObjectName(QStringLiteral("minimumPixels"));
    // Editable, because the resolutions offered are the ones this system is
    // showing and the ones people usually mean, which is not every resolution
    // anybody might want a threshold at.
    m_minimumPixels->setEditable(true);
    m_minimumPixels->setInsertPolicy(QComboBox::NoInsert);
    m_minimumPixels->setToolTip(i18n("Each output is checked independently. Scale only on outputs at least this large. Choose one of your screens, or type a resolution such as 1920x1080. Application rules can override it."));
    upscaleFillResolutions(m_minimumPixels);
    layout->addRow(i18n("Smallest output to scale on:"), m_minimumPixels);
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

void UpscaleEffectConfig::addDisplayControls(QFormLayout *layout)
{
    m_osd->setObjectName(QStringLiteral("osd"));
    m_osdDetection->setObjectName(QStringLiteral("osdDetection"));
    m_osdSummary->setObjectName(QStringLiteral("osdSummary"));
    m_osdStatistics->setObjectName(QStringLiteral("osdStatistics"));
    m_osdDeveloper->setObjectName(QStringLiteral("osdDeveloper"));
    m_osdPosition->setObjectName(QStringLiteral("osdPosition"));
    m_osdTimeout->setObjectName(QStringLiteral("osdTimeout"));
    // The order is the stored one in upscaleconfig.kcfg.
    m_osdPosition->addItems({i18n("Top left"), i18n("Top right"), i18n("Bottom left"), i18n("Bottom right")});
    m_osdTimeout->setRange(1, 60);
    m_osdTimeout->setSuffix(i18n(" s"));
    layout->addRow(m_osd);
    layout->addRow(m_osdDetection);
    layout->addRow(m_osdSummary);
    layout->addRow(i18n("Announcement timeout:"), m_osdTimeout);
    m_osdStatistics->setToolTip(i18n("Keeps the frame rate the screen actually presented on screen while a game is "
                                     "running, with the slowest frames beside the average, because an average alone "
                                     "hides stutter."));
    layout->addRow(m_osdStatistics);
    // Only the view that stays on screen during play is worth moving. The
    // announcement appears in the top left and the developer information in
    // the bottom right, so that three different things are never one block.
    m_osdPosition->setToolTip(i18n("Where the frame rate is shown on the game's screen."));
    layout->addRow(i18n("Frame rate position:"), m_osdPosition);
    layout->addRow(m_osdDeveloper);
    // A Debug build shows statistics and developer information unless the
    // user has said otherwise; a release build shows only the announcement.
    // The defaults live in upscaleconfig.kcfg, not here.
    for (QCheckBox *box : {m_osd, m_osdDetection, m_osdSummary, m_osdStatistics, m_osdDeveloper}) {
        connect(box, &QCheckBox::toggled, this, [this]() {
            updatePreview();
            setNeedsSave(true);
        });
    }
    connect(m_osdTimeout, &QSpinBox::valueChanged, this, [this]() {
        setNeedsSave(true);
    });
    connect(m_osdPosition, &QComboBox::currentIndexChanged, this, [this]() {
        setNeedsSave(true);
    });
}

// The application list is a different kind of setting from the rest of this
// page: it is a list the effect ships and the user edits, kept in its own file
// so that a new package can deliver a corrected entry without touching what
// the user changed. Its restore is therefore separate from this page's
// Defaults, which restores the values above and leaves the list alone.
void UpscaleEffectConfig::addApplicationControls(QFormLayout *layout)
{
    m_unknown = new QCheckBox(i18n("Also ask applications that are not in the list"), widget());
    m_unknown->setToolTip(i18n("Every application is asked for the resolution below when it starts, not only the ones "
                               "listed here. Nothing is known in advance about how an application answers, so one may "
                               "keep its own resolution or open at the wrong size."));
    layout->addRow(i18n("Unlisted applications:"), m_unknown);
    connect(m_unknown, &QCheckBox::toggled, this, [this]() {
        setNeedsSave(true);
    });

    m_editor = new UpscaleApplicationEditor(widget());
    layout->addRow(i18n("Applications:"), m_editor);
    connect(m_editor, &UpscaleApplicationEditor::changed, this, [this]() {
        setNeedsSave(true);
        updateApplicationSummary();
    });

    m_applications = new QLabel(widget());
    m_applications->setObjectName(QStringLiteral("applicationSummary"));
    m_applications->setTextFormat(Qt::PlainText);
    m_applications->setWordWrap(true);
    layout->addRow(QString(), m_applications);
    m_resetApplications = new QPushButton(i18n("Restore the shipped application list"), widget());
    m_resetApplications->setObjectName(QStringLiteral("resetApplications"));
    layout->addRow(QString(), m_resetApplications);
    connect(m_resetApplications, &QPushButton::clicked, this, &UpscaleEffectConfig::resetApplications);
    updateApplicationSummary();
}

void UpscaleEffectConfig::updateApplicationSummary()
{
    const bool customized = UpscaleApplicationEditor::customized();
    m_applications->setText(customized
                                ? i18n("The list differs from the one this version ships.")
                                : i18n("The list is the one this version ships, and follows every update."));
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
        if (preset != ResolutionPreset::Automatic && preset != ResolutionPreset::Custom) {
            m_percentage->setValue(qRound(ratio * 100));
        }
        m_preview->setText(preset == ResolutionPreset::Automatic
                               ? i18n("Automatic uses the supplied buffer and sends no resolution request.")
                               : i18n("%1% — %2 × %3 physical pixels. Select this resolution in the game. Scaling follows the actual supplied buffer, even when it differs.",
                                      QString::number(ratio * 100, 'f', preset == ResolutionPreset::Custom || preset == ResolutionPreset::Native || preset == ResolutionPreset::Performance ? 0 : 1),
                                      desired.width, desired.height));
        if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()))) {
            m_preview->setText(i18n("This output is at or below the pixel threshold: no resolution request or upscaling, unless an application overrides the threshold."));
        }
    }
    m_strength->setEnabled(m_sharpening->isChecked());
    m_strengthLabel->setText(i18n("%1% (0% bypasses sharpening)", m_strength->value()));
    // Switching the display off hides every mode without changing what those
    // modes are set to, so their controls stay readable but inactive.
    for (QCheckBox *control : {m_osdDetection, m_osdSummary, m_osdStatistics, m_osdDeveloper}) {
        control->setEnabled(m_osd->isChecked());
    }
    m_osdTimeout->setEnabled(m_osd->isChecked() && (m_osdDetection->isChecked() || m_osdSummary->isChecked()));
    m_osdPosition->setEnabled(m_osd->isChecked() && m_osdStatistics->isChecked());
}

void UpscaleEffectConfig::showSettings()
{
    m_enabled->setChecked(UpscaleConfig::enabled());
    m_percentage->setValue(UpscaleConfig::percentage());
    m_preset->setCurrentIndex(UpscaleConfig::preset());
    upscaleSelectResolution(m_minimumPixels, UpscaleConfig::minimumPixels());
    m_sharpening->setChecked(UpscaleConfig::sharpening());
    m_strength->setValue(UpscaleConfig::strength());
    m_osd->setChecked(UpscaleConfig::osd());
    m_osdDetection->setChecked(UpscaleConfig::osdDetection());
    m_osdSummary->setChecked(UpscaleConfig::osdSummary());
    m_osdStatistics->setChecked(UpscaleConfig::osdStatistics());
    m_osdDeveloper->setChecked(UpscaleConfig::osdDeveloper());
    m_osdPosition->setCurrentIndex(int(upscaleCorner(UpscaleConfig::osdPosition())));
    m_osdTimeout->setValue(UpscaleConfig::osdTimeout());
    m_unknown->setChecked(UpscaleConfig::unknownApplications());
    updatePreview();
}

void UpscaleEffectConfig::applySettings()
{
    UpscaleConfig::setEnabled(m_enabled->isChecked());
    UpscaleConfig::setPreset(m_preset->currentIndex());
    UpscaleConfig::setPercentage(m_percentage->value());
    UpscaleConfig::setMinimumPixels(upscaleResolutionPixels(m_minimumPixels, UpscaleConfig::minimumPixels()));
    UpscaleConfig::setSharpening(m_sharpening->isChecked());
    UpscaleConfig::setStrength(m_strength->value());
    UpscaleConfig::setOsd(m_osd->isChecked());
    UpscaleConfig::setOsdDetection(m_osdDetection->isChecked());
    UpscaleConfig::setOsdSummary(m_osdSummary->isChecked());
    UpscaleConfig::setOsdStatistics(m_osdStatistics->isChecked());
    UpscaleConfig::setOsdDeveloper(m_osdDeveloper->isChecked());
    UpscaleConfig::setOsdPosition(m_osdPosition->currentIndex());
    UpscaleConfig::setOsdTimeout(m_osdTimeout->value());
    UpscaleConfig::setUnknownApplications(m_unknown->isChecked());
    // A value equal to the current default is stored as no entry at all, so a
    // build type's default is never written back as if the user chose it.
    UpscaleConfig::self()->save();
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
    refreshStatus();
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
    refreshStatus();
}

// Whether the identity the running effect reported is this build's own. The
// same commit can be rebuilt with another branch, date or Qt version.
static bool sameAsInstalled(const QString &loaded)
{
#if UPSCALE_BUILD_INFO
    return loaded == UpscaleBuildInfo::describe();
#else
    Q_UNUSED(loaded)
    return false;
#endif
}

QString UpscaleEffectConfig::installedBuild()
{
#if UPSCALE_BUILD_INFO
    const QString branch = UpscaleBuildInfo::branch();
    const QString revision = UpscaleBuildInfo::revision();
    return QStringLiteral("%1 %2 %3 %4").arg(UpscaleBuildInfo::baseVersion(), revision.isEmpty() ? i18n("unknown revision") : revision, UpscaleBuildInfo::buildDate(), branch.isEmpty() ? i18n("no branch or tag recorded") : branch);
#else
    return i18n("unknown");
#endif
}

void UpscaleEffectConfig::refreshStatus()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("supportInformation"));
    message << QStringLiteral("upscale");
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError() || reply.value().isEmpty()) {
            m_build->setText(i18n("%1\nRunning in KWin: unknown", installedBuild()));
            m_status->setText(i18n("Live status unavailable. Enable Upscale in Desktop Effects, then refresh while the game is running."));
        } else {
            showSupportInformation(reply.value());
        }
        watcher->deleteLater();
    });
}

void UpscaleEffectConfig::showSupportInformation(const QString &information)
{
    QString loaded;
    m_status->setText(upscaleReportedStatus(information, &loaded));
    // The compositor keeps a plugin it has already loaded, so an installed
    // update is not the build that is running until the session restarts.
    // Saying so is the only honest way to report the difference.
    m_build->setText(!loaded.isEmpty() && sameAsInstalled(loaded)
                         ? installedBuild()
                         : i18n("%1\nRunning in KWin: %2", installedBuild(), loaded.isEmpty() ? i18n("unknown") : loaded));
}

} // namespace KWin

#include "upscale_config.moc"
