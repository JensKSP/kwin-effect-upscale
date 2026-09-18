/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "buildtype.h"
#include "supportinformation.h"
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
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

class UpscaleConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void presetsAndKeyboard();
    void saveAndRestore();
    void displayDefaults();
    void readsWhatTheCompositorReported();
};

void UpscaleConfigTest::presetsAndKeyboard()
{
    // KCModule's outer layout belongs to the hosting widget.
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
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
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
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

void UpscaleConfigTest::displayDefaults()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    module.defaults();
    QCheckBox *osd = module.widget()->findChild<QCheckBox *>(QStringLiteral("osd"));
    QCheckBox *detection = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdDetection"));
    QCheckBox *statistics = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdStatistics"));
    QCheckBox *developer = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdDeveloper"));
    QSpinBox *timeout = module.widget()->findChild<QSpinBox *>(QStringLiteral("osdTimeout"));
    QLabel *build = module.widget()->findChild<QLabel *>(QStringLiteral("build"));
    QVERIFY(build);
    // Built without the generated identity, as an upstream copy inside KWin
    // would be, the page still says something rather than showing a blank row.
    QVERIFY(!build->text().isEmpty());
    QVERIFY(osd);
    QVERIFY(detection);
    QVERIFY(statistics);
    QVERIFY(developer);
    QVERIFY(timeout);
    // The announcement is on in both build types; the persistent views follow
    // the build configuration of this binary and nothing else.
    QVERIFY(osd->isChecked());
    QVERIFY(detection->isChecked());
    QCOMPARE(timeout->value(), 3);
    QCOMPARE(statistics->isChecked(), KWin::upscaleDebugBuild);
    QCOMPARE(developer->isChecked(), KWin::upscaleDebugBuild);
    // The master switch hides every mode without changing what they are set to.
    osd->setChecked(false);
    QVERIFY(!statistics->isEnabled());
    QVERIFY(statistics->isChecked() == KWin::upscaleDebugBuild);
    osd->setChecked(true);
    QVERIFY(statistics->isEnabled());

    const auto stored = []() {
        return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    };
    // An explicit choice against the build default is kept as an override, so
    // it survives a later build of the other type.
    developer->setChecked(!KWin::upscaleDebugBuild);
    module.save();
    QCOMPARE(stored().readEntry("OsdDeveloper", KWin::upscaleDebugBuild), !KWin::upscaleDebugBuild);
    module.load();
    QCOMPARE(developer->isChecked(), !KWin::upscaleDebugBuild);
    // Choosing what this build defaults to writes no override at all, rather
    // than freezing this build's inferred default into the configuration.
    developer->setChecked(KWin::upscaleDebugBuild);
    module.save();
    QVERIFY(!stored().hasKey("OsdDeveloper"));
}

void UpscaleConfigTest::readsWhatTheCompositorReported()
{
    // Exactly the shape KWin's supportInformation produces for this effect.
    const QString reported = QStringLiteral("upscale:\nbuild: upscale 0.1.0 (branch test), built now\n"
                                            "status: Desired: Automatic (no request)\nSupplied input: 1280 × 720\n"
                                            "Inactive: the window is not fullscreen.\n");
    QString loaded;
    const QString status = KWin::upscaleReportedStatus(reported, &loaded);
    QCOMPARE(loaded, QStringLiteral("upscale 0.1.0 (branch test), built now"));
    QVERIFY2(status.startsWith(QStringLiteral("Desired: Automatic")), qPrintable(status));
    QVERIFY(status.contains(QStringLiteral("Inactive: the window is not fullscreen.")));
    // Neither the effect's name nor the property names belong on the page.
    QVERIFY(!status.contains(QStringLiteral("upscale:")));
    QVERIFY(!status.contains(QStringLiteral("status:")));
    QVERIFY(!status.contains(QStringLiteral("build:")));

    // An effect built without the generated identity reports no build, and the
    // status still has to come through.
    QString missing;
    QCOMPARE(KWin::upscaleReportedStatus(QStringLiteral("upscale:\nstatus: nothing to report\n"), &missing),
             QStringLiteral("nothing to report"));
    QVERIFY(missing.isEmpty());
    QCOMPARE(KWin::upscaleReportedStatus(QString(), nullptr), QString());
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
