/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The application list as a whole: "All applications" pinned first, a game
// following what it shows, the defaults of System Settings reaching it and
// nothing else, and the list leaving the page as a file and coming back.

#include "application.h"
#include "applicationeditor.h"
#include "resolution.h"
#include "resolutionchoice.h"
#include "upscale_config.h"
#include "upscaleconfig.h"

#include "settings_fixture.h"

#include <KPluginMetaData>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>
#include <ranges>

class ApplicationListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void showsTheGlobalSettingsAsTheFirstEntry();
    void aGameFollowsWhatAllApplicationsShows();
    void defaultsRestoreOnlyTheGlobalSettings();
    void exportsAndImportsTheList();
};

void ApplicationListTest::init()
{
    QFile::remove(upscaleUserApplicationFile());
    KWin::upscaleReloadApplications();
}

void ApplicationListTest::showsTheGlobalSettingsAsTheFirstEntry()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *details = editor->findChild<QStackedWidget *>();
    auto *all = module.widget()->findChild<QWidget *>(QStringLiteral("allApplications"));
    auto *remove = editor->findChild<QPushButton *>(QStringLiteral("applicationRemove"));
    auto *up = editor->findChild<QPushButton *>(QStringLiteral("applicationMoveUp"));
    auto *down = editor->findChild<QPushButton *>(QStringLiteral("applicationMoveDown"));
    QVERIFY(list && details && all && remove && up && down);
    // Pinned first, and checked like every other row: whether the global
    // profile acts for the windows no other entry matches. Off by default.
    QCOMPARE(list->item(0)->text(), QStringLiteral("All applications"));
    QVERIFY(list->item(0)->flags() & Qt::ItemIsUserCheckable);
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    list->setCurrentRow(0);
    QCOMPARE(details->currentWidget(), all);
    QVERIFY(!remove->isEnabled() && !up->isEnabled() && !down->isEnabled());
    // The methods for applications not in the list can be set before the
    // check box that uses them is.
    auto *method = all->findChild<QComboBox *>(QStringLiteral("method0"));
    QVERIFY(method && method->isEnabled());
    list->item(0)->setCheckState(Qt::Checked);
    QVERIFY(module.needsSave());
    module.save();
    QVERIFY(KWin::UpscaleConfig::unlistedApplications());
    module.load();
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    // Every other row shows a game's tabs instead.
    list->setCurrentRow(1);
    QVERIFY(details->currentWidget() != all);
}

// A game's Global choices name what "All applications" shows, applied or not,
// and its controls behave as the global ones do, with the preview computed
// from the values the game would use.
void ApplicationListTest::aGameFollowsWhatAllApplicationsShows()
{
    using KWin::ResolutionPreset;
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *globalPreset = module.widget()->findChild<QComboBox *>(QStringLiteral("preset"));
    auto *globalMinimum = module.widget()->findChild<QComboBox *>(QStringLiteral("minimumPixels"));
    auto *preset = editor->findChild<QComboBox *>(QStringLiteral("Resolution"));
    auto *scale = editor->findChild<QDoubleSpinBox *>(QStringLiteral("Percentage"));
    auto *minimum = editor->findChild<QComboBox *>(QStringLiteral("MinimumPixels"));
    auto *sharpening = editor->findChild<QComboBox *>(QStringLiteral("Sharpening"));
    auto *strength = editor->findChild<QSpinBox *>(QStringLiteral("Strength"));
    auto *preview = editor->findChild<QLabel *>(QStringLiteral("applicationPreview"));
    QVERIFY(list && globalPreset && globalMinimum && preset && scale && minimum && sharpening && strength && preview);
    list->setCurrentRow(0);
    globalPreset->setCurrentIndex(int(ResolutionPreset::Balanced));
    // The test screen is small, and a limit above it would say only that it
    // is not upscaled.
    globalMinimum->setCurrentIndex(0);
    editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"))->click();
    QCOMPARE(preset->currentText(), QStringLiteral("Global (Balanced)"));
    QCOMPARE(minimum->currentText(), QStringLiteral("Global (Any screen)"));
    const QScreen *screen = QGuiApplication::screens().constFirst();
    const KWin::UpscaleSize output{screen->geometry().width(), screen->geometry().height()};
    const auto rendered = [output](ResolutionPreset preset, int basisPoints) {
        const KWin::UpscaleSize size = KWin::desiredResolution(output, preset, basisPoints);
        return QStringLiteral("renders at %1 × %2").arg(size.width).arg(size.height);
    };
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Balanced, 0)), qPrintable(preview->text()));

    // Stepping off Global starts from the value followed, and stating a scale
    // is choosing Custom, as moving the global slider is.
    QCOMPARE(scale->value(), scale->minimum());
    QTest::keyClick(scale, Qt::Key_Up);
    QCOMPARE(scale->value(), 58.82);
    QCOMPARE(preset->currentIndex(), int(ResolutionPreset::Custom) + 1);
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Custom, 5882)), qPrintable(preview->text()));
    // Any other preset leaves no use for a scale of its own.
    preset->setCurrentIndex(int(ResolutionPreset::Quality) + 1);
    QCOMPARE(scale->value(), scale->minimum());
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Quality, 0)), qPrintable(preview->text()));

    // Nothing is greyed out by a switch being off: the strength can be set
    // before the sharpening that uses it.
    QCOMPARE(sharpening->currentText(), QStringLiteral("Global (Off)"));
    QVERIFY(strength->isEnabled());

    // A limit above every screen leaves the game alone, and one too large to
    // count is held at the largest a limit can be rather than wrapping round.
    minimum->setCurrentText(QStringLiteral("7680x4320"));
    QVERIFY2(preview->text().contains(QStringLiteral("not upscaled")), qPrintable(preview->text()));
    minimum->setCurrentText(QStringLiteral("999999x999999"));
    QCOMPARE(KWin::upscaleResolutionPixels(minimum, -1), std::numeric_limits<int>::max());

    // Defaults restore the global settings while the game is shown, and its
    // Global choices follow at once.
    // In the user's locale, as the field itself shows the number.
    const auto global = [](double percentage) {
        return QStringLiteral("Global (%1%)").arg(QLocale().toString(percentage, 'f', 2));
    };
    QCOMPARE(scale->specialValueText(), global(58.82));
    module.defaults();
    QCOMPARE(scale->specialValueText(), global(66.67));
}

// System Settings' Defaults is the global profile's: it restores "All
// applications" and leaves the list, which has a restore of its own.
void ApplicationListTest::defaultsRestoreOnlyTheGlobalSettings()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *preset = module.widget()->findChild<QComboBox *>(QStringLiteral("preset"));
    auto *sharpening = module.widget()->findChild<QCheckBox *>(QStringLiteral("sharpening"));
    QVERIFY(list && preset && sharpening);
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    add->click();
    const int entries = list->count();
    preset->setCurrentIndex(0);
    sharpening->setChecked(true);
    module.defaults();
    QCOMPARE(list->count(), entries);
    // Defaults put the generated configuration back to upscaleconfig.kcfg's
    // values, which the page then shows.
    QCOMPARE(preset->currentIndex(), KWin::UpscaleConfig::resolution());
    QVERIFY(preset->currentIndex() != 0);
    QVERIFY(!sharpening->isChecked());
}

// The file carries every field of every entry, so that it stands on its own;
// importing it back is an edit like any other, stored on Apply.
void ApplicationListTest::exportsAndImportsTheList()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("list.conf"));
    {
        QWidget host;
        KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
        auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
        auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
        auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
        auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
        editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"))->click();
        name->clear();
        QTest::keyClicks(name, QStringLiteral("Exported"));
        QTest::keyClicks(instance, QStringLiteral("exported"));
        QVERIFY(editor->exportTo(path));
        QCOMPARE(list->count() - 1, int(KWin::upscaleReadApplicationFile(path).size()));
    }
    // Nothing was applied, so the list is still the shipped one here.
    QVERIFY(!QFile::exists(upscaleUserApplicationFile()) || !QFile(upscaleUserApplicationFile()).size());
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    const int shipped = list->count();
    QVERIFY(editor->importFrom(path) > 0);
    QCOMPARE(list->count(), shipped + 1);
    QVERIFY(module.needsSave());
    module.save();
    const std::vector<KWin::UpscaleApplication> &stored = KWin::upscaleApplications();
    QVERIFY(std::ranges::any_of(stored, [](const KWin::UpscaleApplication &application) {
        return application.name == QStringLiteral("Exported") && application.instance == QStringLiteral("exported");
    }));
    // The shipped entries came back unchanged, so nothing is stored for them.
    const QString user = [] {
        QFile file(upscaleUserApplicationFile());
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }();
    QVERIFY2(!user.contains(QStringLiteral("[Application-supertuxkart]")), qPrintable(user));
}

int main(int argc, char **argv)
{
    return runSettingsTest<ApplicationListTest>(argc, argv);
}

#include "application_list_test.moc"
