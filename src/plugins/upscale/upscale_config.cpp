/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale_config.h"

#include "resolution.h"
#include "upscaleconfig.h"

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
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>

K_PLUGIN_FACTORY(UpscaleEffectConfigFactory, registerPlugin<KWin::UpscaleEffectConfig>();)

namespace KWin
{

UpscaleEffectConfig::UpscaleEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_enabled(new QCheckBox(i18n("Enable upscaling"), widget()))
    , m_output(new QComboBox(widget()))
    , m_preset(new QComboBox(widget()))
    , m_percentage(new QSlider(Qt::Horizontal, widget()))
    , m_preview(new QLabel(widget()))
    , m_sharpening(new QCheckBox(i18n("Enable RCAS sharpening"), widget()))
    , m_strength(new QSlider(Qt::Horizontal, widget()))
    , m_strengthLabel(new QLabel(widget()))
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
    layout->addRow(m_sharpening);
    m_strength->setRange(0, 100);
    layout->addRow(i18n("Sharpening strength:"), m_strength);
    layout->addRow(m_strengthLabel);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addRow(m_status);
    auto refresh = new QPushButton(i18n("Refresh supplied-buffer status"), widget());
    layout->addRow(refresh);
    connect(refresh, &QPushButton::clicked, this, &UpscaleEffectConfig::refreshStatus);
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
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &UpscaleEffectConfig::updateOutputs);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &UpscaleEffectConfig::updateOutputs);
    updateOutputs();
    UpscaleEffectConfig::load();
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
    }
    m_strength->setEnabled(m_sharpening->isChecked());
    m_strengthLabel->setText(i18n("%1% (0% bypasses sharpening)", m_strength->value()));
}

void UpscaleEffectConfig::load()
{
    UpscaleConfig::self()->read();
    m_enabled->setChecked(UpscaleConfig::enabled());
    m_percentage->setValue(UpscaleConfig::percentage());
    m_preset->setCurrentIndex(UpscaleConfig::preset());
    m_sharpening->setChecked(UpscaleConfig::sharpening());
    m_strength->setValue(UpscaleConfig::strength());
    updatePreview();
    setNeedsSave(false);
    refreshStatus();
}

void UpscaleEffectConfig::defaults()
{
    // The defaults live in upscaleconfig.kcfg. Repeating them here is how the
    // dialog and the effect start to disagree about what "default" means.
    UpscaleConfig::self()->setDefaults();
    m_enabled->setChecked(UpscaleConfig::enabled());
    m_percentage->setValue(UpscaleConfig::percentage());
    m_preset->setCurrentIndex(UpscaleConfig::preset());
    m_sharpening->setChecked(UpscaleConfig::sharpening());
    m_strength->setValue(UpscaleConfig::strength());
    updatePreview();
    setNeedsSave(true);
}

void UpscaleEffectConfig::save()
{
    UpscaleConfig::setEnabled(m_enabled->isChecked());
    UpscaleConfig::setPreset(m_preset->currentIndex());
    UpscaleConfig::setPercentage(m_percentage->value());
    UpscaleConfig::setSharpening(m_sharpening->isChecked());
    UpscaleConfig::setStrength(m_strength->value());
    UpscaleConfig::self()->save();
    setNeedsSave(false);
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("reconfigureEffect"));
    message << QStringLiteral("upscale");
    QDBusConnection::sessionBus().asyncCall(message);
    refreshStatus();
}

void UpscaleEffectConfig::refreshStatus()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("supportInformation"));
    message << QStringLiteral("upscale");
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        const QDBusPendingReply<QString> reply = *watcher;
        m_status->setText(reply.isError() || reply.value().isEmpty()
                              ? i18n("Live status unavailable. Enable Upscale in Desktop Effects, then refresh while the game is running.")
                              : reply.value());
        watcher->deleteLater();
    });
}

} // namespace KWin

#include "upscale_config.moc"
