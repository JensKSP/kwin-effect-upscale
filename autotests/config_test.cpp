/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "buildtype.h"
#include "supportinformation.h"
#include "upscale_config.h"

#include "settings_fixture.h"

#if __has_include("buildinfo.h")
#include "buildinfo.h"
#endif

#include <KConfigGroup>
#include <KPluginMetaData>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QTest>

// This service only exists on the private bus started by main(). Real KWin
// must never receive configuration or refresh calls from these tests.
class TestEffects : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.Effects")

public:
    QString information;

public Q_SLOTS:
    QString supportInformation(const QString &effect)
    {
        return effect == QStringLiteral("upscale") ? information : QString();
    }
};

class UpscaleConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void presetsAndKeyboard();
    void saveAndRestore();
    void displayDefaults();
    void runningBuildStatus();
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
    QSpinBox *minimum = module.widget()->findChild<QSpinBox *>(QStringLiteral("minimumPixels"));
    QVERIFY(minimum);
    QCOMPARE(minimum->value(), 2073600);
    minimum->setValue(0);
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
    QSpinBox *minimum = module.widget()->findChild<QSpinBox *>(QStringLiteral("minimumPixels"));
    QVERIFY(minimum);
    minimum->setValue(3686400);
    sharpening->setChecked(true);
    strength->setValue(0);
    module.save();
    const KConfigGroup saved(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    QCOMPARE(saved.readEntry("Preset", -1), 6);
    QCOMPARE(saved.readEntry("Percentage", -1), 73);
    QCOMPARE(saved.readEntry("MinimumPixels", -1), 3686400);
    QCOMPARE(saved.readEntry("Strength", -1), 0);
    QCOMPARE(saved.readEntry("Sharpening", false), true);
    module.defaults();
    QCOMPARE(preset->currentIndex(), 0);
    QVERIFY(!sharpening->isChecked());
    module.load();
    QCOMPARE(minimum->value(), 3686400);
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

void UpscaleConfigTest::runningBuildStatus()
{
    TestEffects effects;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.isConnected());
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin")));
    QVERIFY(bus.registerObject(QStringLiteral("/Effects"), &effects, QDBusConnection::ExportAllSlots));
    effects.information = QStringLiteral("upscale:\nbuild: older-running-build\nstatus: Supplied input: 1280 × 720\nDestination: 2560 × 1440");
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    QLabel *build = module.widget()->findChild<QLabel *>(QStringLiteral("build"));
    QLabel *status = module.widget()->findChild<QLabel *>(QStringLiteral("status"));
    QPushButton *refresh = module.widget()->findChild<QPushButton *>(QStringLiteral("refreshStatus"));
    QVERIFY(build);
    QVERIFY(status);
    QVERIFY(refresh);
    QTRY_VERIFY(build->text().contains(QStringLiteral("Running in KWin: older-running-build")));
    QCOMPARE(status->text(), QStringLiteral("Supplied input: 1280 × 720\nDestination: 2560 × 1440"));

#if __has_include("buildinfo.h")
    const QString installed = KWin::UpscaleBuildInfo::describe();
    effects.information = QStringLiteral("upscale:\nbuild: %1\nstatus: Matching build").arg(installed);
    refresh->click();
    QTRY_COMPARE(status->text(), QStringLiteral("Matching build"));
    QVERIFY(!build->text().contains(QStringLiteral("Running in KWin:")));
    // A rebuild can keep the version while changing other identity fields.
    // None of those builds may be mistaken for the installed module.
    const QStringList alternatives = {
        installed + QStringLiteral(" (other branch)"),
        installed + QStringLiteral(", built another day"),
        installed + QStringLiteral(", Qt another version"),
    };
    for (const QString &identity : alternatives) {
        effects.information = QStringLiteral("upscale:\nbuild: %1\nstatus: %1").arg(identity);
        refresh->click();
        QTRY_COMPARE(status->text(), identity);
        QVERIFY(build->text().contains(QStringLiteral("Running in KWin: %1").arg(identity)));
    }
#endif

    // Older effects can return status without a build property. That does not
    // establish that the running effect matches the package now installed.
    effects.information = QStringLiteral("upscale:\nstatus: No build identity available");
    refresh->click();
    QTRY_COMPARE(status->text(), QStringLiteral("No build identity available"));
    QVERIFY(build->text().contains(QStringLiteral("Running in KWin: unknown")));
    QVERIFY(!build->text().contains(QStringLiteral("older-running-build")));

    effects.information = QStringLiteral("upscale:\nbuild: refreshed-running-build\nstatus: Updated");
    refresh->click();
    QTRY_COMPARE(status->text(), QStringLiteral("Updated"));
    QVERIFY(build->text().contains(QStringLiteral("refreshed-running-build")));
    effects.information.clear();
    refresh->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Live status unavailable")));
    QVERIFY(build->text().contains(QStringLiteral("Running in KWin: unknown")));
    QVERIFY(!build->text().contains(QStringLiteral("refreshed-running-build")));

    // Losing the service after a successful reply must clear the old identity
    // just as an empty reply does.
    effects.information = QStringLiteral("upscale:\nbuild: stale-running-build\nstatus: Available");
    refresh->click();
    QTRY_COMPARE(status->text(), QStringLiteral("Available"));
    bus.unregisterObject(QStringLiteral("/Effects"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin")));
    refresh->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Live status unavailable")));
    QVERIFY(build->text().contains(QStringLiteral("Running in KWin: unknown")));
    QVERIFY(!build->text().contains(QStringLiteral("stale-running-build")));
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
    QCOMPARE(KWin::upscaleReportedStatus(QStringLiteral("upscale:\nstatus: nothing to report\n"), &loaded),
             QStringLiteral("nothing to report"));
    QVERIFY(loaded.isEmpty());
    loaded = QStringLiteral("stale build identity");
    QCOMPARE(KWin::upscaleReportedStatus(QString(), &loaded), QString());
    QVERIFY(loaded.isEmpty());
    QCOMPARE(KWin::upscaleReportedStatus(QString(), nullptr), QString());
}

int main(int argc, char **argv)
{
    return runSettingsTest<UpscaleConfigTest>(argc, argv);
}

#include "config_test.moc"
