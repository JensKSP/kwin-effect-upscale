/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "buildtype.h"
#include "placement.h"
#include "resolutionchoice.h"
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
#include <QRegularExpression>
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
    void installedBuildVersion();
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
    // Quality by default. There is no Automatic any more: what it meant was
    // "nobody chose", which a profile now says by storing nothing, and the
    // global default has to be a real reduction or a fresh install would
    // leave the games it ships profiles for at their full resolution.
    QCOMPARE(preset->currentIndex(), 2);
    // The threshold is stored as a pixel count and offered as a resolution,
    // because nobody setting one is thinking of 2073600.
    QComboBox *minimum = module.widget()->findChild<QComboBox *>(QStringLiteral("minimumPixels"));
    QVERIFY(minimum);
    QCOMPARE(KWin::upscaleResolutionPixels(minimum, -1), 2073600);
    QVERIFY2(minimum->currentText().contains(QStringLiteral("1920 × 1080")), qPrintable(minimum->currentText()));
    QVERIFY2(minimum->itemData(0).toInt() == 0, "the first entry is every output");
    minimum->setCurrentIndex(0);
    // Native is the preset that asks for nothing, and says what that means.
    preset->setCurrentIndex(0);
    QVERIFY2(preview->text().contains(QStringLiteral("full resolution")), qPrintable(preview->text()));
    preset->setCurrentIndex(2);
    QCOMPARE(percentage->value(), 67);
    QVERIFY(preview->text().contains(QStringLiteral("66.7%")));
    const QScreen *screen = QGuiApplication::screens().constFirst();
    const QSize output = screen->geometry().size() * screen->devicePixelRatio();
    QVERIFY2(preview->text().contains(QStringLiteral("%1 × %2").arg(qRound(output.width() / 1.5)).arg(qRound(output.height() / 1.5))), qPrintable(preview->text()));
    QTest::keyClick(percentage, Qt::Key_Right);
    // Moving the slider off a preset's exact ratio is choosing Custom.
    QCOMPARE(preset->currentIndex(), 5);
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
    QComboBox *minimum = module.widget()->findChild<QComboBox *>(QStringLiteral("minimumPixels"));
    QVERIFY(minimum);
    // Typed the way a person writes it, with the x on their keyboard.
    minimum->setCurrentText(QStringLiteral("2560x1440"));
    QCOMPARE(KWin::upscaleResolutionPixels(minimum, -1), 3686400);
    sharpening->setChecked(true);
    strength->setValue(0);
    module.save();
    const KConfigGroup saved(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    // Written under its current name. The old Preset key numbered the same
    // presets one higher, so storing under it would have been misread.
    QCOMPARE(saved.readEntry("Resolution", -1), 5);
    QVERIFY(!saved.hasKey("Preset"));
    QCOMPARE(saved.readEntry("Percentage", -1), 73);
    QCOMPARE(saved.readEntry("MinimumPixels", -1), 3686400);
    QCOMPARE(saved.readEntry("Strength", -1), 0);
    QCOMPARE(saved.readEntry("Sharpening", false), true);
    module.defaults();
    // The default is Quality, index 2, not the first entry: see presetsAndKeyboard.
    QCOMPARE(preset->currentIndex(), 2);
    QVERIFY(!sharpening->isChecked());
    module.load();
    QCOMPARE(KWin::upscaleResolutionPixels(minimum, -1), 3686400);
    // Custom, which is the last of six presets now that Automatic is gone.
    QCOMPARE(preset->currentIndex(), 5);
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
    QCheckBox *detection = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdDetection"));
    QCheckBox *statistics = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdStatistics"));
    QCheckBox *developer = module.widget()->findChild<QCheckBox *>(QStringLiteral("osdDeveloper"));
    QSpinBox *timeout = module.widget()->findChild<QSpinBox *>(QStringLiteral("osdTimeout"));
    QComboBox *announcementPosition = module.widget()->findChild<QComboBox *>(QStringLiteral("osdAnnouncementPosition"));
    QComboBox *position = module.widget()->findChild<QComboBox *>(QStringLiteral("osdStatisticsPosition"));
    QComboBox *developerPosition = module.widget()->findChild<QComboBox *>(QStringLiteral("osdDeveloperPosition"));
    QLabel *build = module.widget()->findChild<QLabel *>(QStringLiteral("build"));
    QVERIFY(build);
    // Built without the generated identity, as an upstream copy inside KWin
    // would be, the page still says something rather than showing a blank row.
    QVERIFY(!build->text().isEmpty());
    QVERIFY2(!module.widget()->findChild<QCheckBox *>(QStringLiteral("osd")),
             "the settings page still offers a switch above the four displays");
    QVERIFY(detection);
    QVERIFY(statistics);
    QVERIFY(developer);
    QVERIFY(timeout);
    QVERIFY(announcementPosition);
    QVERIFY(position);
    QVERIFY(developerPosition);
    // The announcement is on in both build types; the persistent views follow
    // the build configuration of this binary and nothing else.
    QVERIFY(detection->isChecked());
    QCOMPARE(timeout->value(), 3);
    QCOMPARE(statistics->isChecked(), KWin::upscaleDebugBuild);
    QCOMPARE(developer->isChecked(), KWin::upscaleDebugBuild);
    // Each display is its own switch now: nothing above them can grey one
    // out, so a mode stays available whatever the other three are set to.
    QVERIFY(statistics->isEnabled());
    QVERIFY(developer->isEnabled());
    // Each display starts in its own corner, leaving the fourth free for the
    // interactive panel, and a corner is worth choosing only while the display
    // that would occupy it is switched on.
    QCOMPARE(announcementPosition->currentIndex(), int(KWin::UpscaleCorner::TopLeft));
    QCOMPARE(position->currentIndex(), int(KWin::UpscaleCorner::TopRight));
    QCOMPARE(developerPosition->currentIndex(), int(KWin::UpscaleCorner::BottomRight));
    statistics->setChecked(false);
    QVERIFY(!position->isEnabled());
    statistics->setChecked(true);

    // Choosing a corner another display holds moves that display to the next
    // free one. The announcement takes the heads-up's corner; the heads-up
    // steps on to the free corner rather than swapping into the vacated one.
    announcementPosition->setCurrentIndex(int(KWin::UpscaleCorner::TopRight));
    QCOMPARE(announcementPosition->currentIndex(), int(KWin::UpscaleCorner::TopRight));
    QCOMPARE(position->currentIndex(), int(KWin::UpscaleCorner::BottomLeft));
    QCOMPARE(developerPosition->currentIndex(), int(KWin::UpscaleCorner::BottomRight));

    // Whatever is chosen, the three never name the same corner.
    for (int corner = 0; corner < KWin::upscaleCornerCount; ++corner) {
        developerPosition->setCurrentIndex(corner);
        QCOMPARE(developerPosition->currentIndex(), corner);
        QVERIFY2(announcementPosition->currentIndex() != position->currentIndex()
                     && position->currentIndex() != developerPosition->currentIndex()
                     && announcementPosition->currentIndex() != developerPosition->currentIndex(),
                 "two displays were left holding the same corner");
    }
    statistics->setChecked(true);
    QVERIFY(position->isEnabled());

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
    // A chosen corner is stored and read back as the corner, not as a number
    // that happens to survive: the page is what a person sets it with.
    position->setCurrentIndex(int(KWin::UpscaleCorner::BottomLeft));
    QVERIFY(module.needsSave());
    module.save();
    QCOMPARE(stored().readEntry("OsdStatisticsPosition", -1), int(KWin::UpscaleCorner::BottomLeft));
    module.load();
    QCOMPARE(position->currentIndex(), int(KWin::UpscaleCorner::BottomLeft));
    module.defaults();
    QCOMPARE(position->currentIndex(), int(KWin::UpscaleCorner::TopRight));
}

void UpscaleConfigTest::installedBuildVersion()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    const QLabel *build = module.widget()->findChild<QLabel *>(QStringLiteral("build"));
    QVERIFY(build);
#if __has_include("buildinfo.h")
    // The version exactly as this repository's version rule names the build,
    // and nothing else: the full record belongs to the log and the developer
    // view, not to the settings page.
    QCOMPARE(build->text(), KWin::UpscaleBuildInfo::version());
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+")).match(build->text()).hasMatch());
#else
    QCOMPARE(build->text(), QStringLiteral("Unknown"));
#endif
    // The author comes from the effect's metadata, found by plugin ID beside
    // this test in the build tree, and is not repeated in the page's code.
    const QLabel *author = module.widget()->findChild<QLabel *>(QStringLiteral("author"));
    QVERIFY2(author, "no author row: the effect's metadata was not found");
    QCOMPARE(author->text(), QStringLiteral("Jens Köhler"));
    // Nothing reports the running effect any more.
    QVERIFY(!module.widget()->findChild<QLabel *>(QStringLiteral("status")));
    QVERIFY(!module.widget()->findChild<QPushButton *>(QStringLiteral("refreshStatus")));
}

int main(int argc, char **argv)
{
    return runSettingsTest<UpscaleConfigTest>(argc, argv);
}

#include "config_test.moc"
