/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "upscale_config.h"

#include <KConfigGroup>
#include <KPluginMetaData>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QScreen>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>

class UpscaleConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void presetsAndKeyboard();
    void saveAndRestore();
};

void UpscaleConfigTest::presetsAndKeyboard()
{
    KWin::UpscaleEffectConfig module(nullptr, KPluginMetaData());
    module.defaults();
    QComboBox *preset = module.widget()->findChild<QComboBox *>(QStringLiteral("preset"));
    QSlider *percentage = module.widget()->findChild<QSlider *>(QStringLiteral("percentage"));
    QLabel *preview = module.widget()->findChild<QLabel *>(QStringLiteral("preview"));
    QVERIFY(preset);
    QVERIFY(percentage);
    QVERIFY(preview);
    QCOMPARE(preset->currentIndex(), 0);
    QVERIFY(preview->text().contains(QStringLiteral("no resolution request")));
    preset->setCurrentIndex(3);
    QCOMPARE(percentage->value(), 67);
    QVERIFY(preview->text().contains(QStringLiteral("66.7%")));
    const QScreen *screen = QGuiApplication::screens().constFirst();
    const QSize output = screen->geometry().size() * screen->devicePixelRatio();
    QVERIFY(preview->text().contains(QStringLiteral("%1 × %2").arg(qRound(output.width() / 1.5)).arg(qRound(output.height() / 1.5))));
    QTest::keyClick(percentage, Qt::Key_Right);
    QCOMPARE(preset->currentIndex(), 6);
    QCOMPARE(percentage->value(), 68);
    QVERIFY(preview->text().contains(QStringLiteral("68%")));
    percentage->setValue(50);
    QTest::keyClick(percentage, Qt::Key_Left);
    QCOMPARE(percentage->value(), 50);
    percentage->setValue(100);
    QTest::keyClick(percentage, Qt::Key_Right);
    QCOMPARE(percentage->value(), 100);
}

void UpscaleConfigTest::saveAndRestore()
{
    KWin::UpscaleEffectConfig module(nullptr, KPluginMetaData());
    module.defaults();
    QComboBox *preset = module.widget()->findChild<QComboBox *>(QStringLiteral("preset"));
    QSlider *percentage = module.widget()->findChild<QSlider *>(QStringLiteral("percentage"));
    QCheckBox *sharpening = module.widget()->findChild<QCheckBox *>(QStringLiteral("sharpening"));
    QSlider *strength = module.widget()->findChild<QSlider *>(QStringLiteral("strength"));
    QVERIFY(preset);
    QVERIFY(percentage);
    QVERIFY(sharpening);
    QVERIFY(strength);
    QVERIFY(!sharpening->isChecked());
    QVERIFY(!strength->isEnabled());
    percentage->setValue(73);
    sharpening->setChecked(true);
    strength->setValue(0);
    module.save();
    const KConfigGroup saved(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    QCOMPARE(saved.readEntry("Preset", -1), 6);
    QCOMPARE(saved.readEntry("Percentage", -1), 73);
    QCOMPARE(saved.readEntry("Strength", -1), 0);
    QCOMPARE(saved.readEntry("Sharpening", false), true);
    module.defaults();
    QCOMPARE(preset->currentIndex(), 0);
    QVERIFY(!sharpening->isChecked());
    module.load();
    QCOMPARE(preset->currentIndex(), 6);
    QCOMPARE(percentage->value(), 73);
    QVERIFY(sharpening->isChecked());
    QVERIFY(strength->isEnabled());
    QCOMPARE(strength->value(), 0);
}

int main(int argc, char **argv)
{
    QTemporaryDir configuration(QDir::currentPath() + QStringLiteral("/config-test-XXXXXX"));
    if (!configuration.isValid()) {
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", configuration.path().toUtf8());
    // Never connect the settings test to a developer's session bus.
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-upscale-test-bus");
    QApplication application(argc, argv);
    UpscaleConfigTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "config_test.moc"
