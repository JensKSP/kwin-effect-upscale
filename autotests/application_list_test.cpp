/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The application list as a whole: "All applications" pinned first, a game
// following what it shows, the defaults of System Settings reaching it and
// nothing else, and the list leaving the page as a file and coming back.

#include "application.h"
#include "applicationeditor.h"
#include "methodcontrols.h"
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
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <limits>
#include <ranges>

// What a game's row shows: its own value, an inherited one, or a mixture of
// the two, which the rule forbids.
enum State {
    Follows,
    States,
    Inconsistent,
};

static State inheritance(const QWidget *editor, const QString &key)
{
    const QWidget *label = editor->findChild<QWidget *>(key + QStringLiteral("Name"));
    const QWidget *number = editor->findChild<QWidget *>(key + QStringLiteral("Value"));
    const QWidget *control = editor->findChild<QWidget *>(key);
    const bool bold = (label ? label : control)->font().bold();
    const bool upright = !(number ? number : control)->font().italic();
    const bool reset = editor->findChild<QToolButton *>(key + QStringLiteral("Reset"))->isEnabled();
    if (bold != reset || reset != upright) {
        return Inconsistent;
    }
    return bold ? States : Follows;
}

class ApplicationListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void showsTheGlobalSettingsAsTheFirstEntry();
    void aGameFollowsWhatAllApplicationsShows();
    void aGameInheritsItsMethods();
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
    // The global methods can be set before the check box that lets them reach
    // applications not in the list is.
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

// A game's page shows the values the game would use: the global ones until it
// states its own, which follow Qt Designer's rule - a bold name and an enabled
// reset button that makes them follow again. Its controls behave
// as the global ones do, with the preview computed from the values it uses.
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
    auto *presetReset = editor->findChild<QToolButton *>(QStringLiteral("ResolutionReset"));
    auto *scale = editor->findChild<QSlider *>(QStringLiteral("Percentage"));
    auto *scaleField = editor->findChild<QDoubleSpinBox *>(QStringLiteral("PercentageValue"));
    auto *minimum = editor->findChild<QComboBox *>(QStringLiteral("MinimumPixels"));
    auto *sharpening = editor->findChild<QCheckBox *>(QStringLiteral("Sharpening"));
    auto *strength = editor->findChild<QSlider *>(QStringLiteral("Strength"));
    auto *preview = editor->findChild<QLabel *>(QStringLiteral("applicationPreview"));
    QVERIFY(list && globalPreset && globalMinimum && preset && presetReset && scale && scaleField && minimum && sharpening
            && strength && preview);
    // Qt Designer's rule: a value the game states has a bold name and an
    // enabled reset button, and is shown upright; one it follows has neither
    // and is shown in italic; never a mixture. The name is the label, or a
    // switch's own text, and a slider's value is its number field.
    const auto state = [editor](const QString &key) {
        return inheritance(editor, key);
    };
    list->setCurrentRow(0);
    globalPreset->setCurrentIndex(int(ResolutionPreset::Balanced));
    // The test screen is small, and a limit above it would say only that it
    // is not upscaled.
    globalMinimum->setCurrentIndex(0);
    editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"))->click();
    // A new game states nothing, so every control shows the global value in
    // a plain name, with nothing to reset.
    QCOMPARE(preset->currentText(), QStringLiteral("Balanced"));
    QCOMPARE(state(QStringLiteral("Resolution")), Follows);
    QCOMPARE(minimum->currentText(), QStringLiteral("Any screen"));
    // Under any preset but Custom the scale shows that preset's share, as the
    // global page shows it.
    QCOMPARE(scale->value(), qRound(KWin::resolutionRatio(ResolutionPreset::Balanced, 0) * 10000));
    const QScreen *screen = QGuiApplication::screens().constFirst();
    const KWin::UpscaleSize output{screen->geometry().width(), screen->geometry().height()};
    const auto rendered = [output](ResolutionPreset preset, int basisPoints) {
        const KWin::UpscaleSize size = KWin::desiredResolution(output, preset, basisPoints);
        return QStringLiteral("renders at %1 × %2").arg(size.width).arg(size.height);
    };
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Balanced, 0)), qPrintable(preview->text()));

    // Stating a scale is choosing Custom, as moving the global slider is, and
    // both are then the game's own.
    QTest::keyClick(scaleField, Qt::Key_Up);
    QCOMPARE(state(QStringLiteral("Percentage")), States);
    QCOMPARE(state(QStringLiteral("Resolution")), States);
    QCOMPARE(preset->currentIndex(), int(ResolutionPreset::Custom));
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Custom, scale->value())), qPrintable(preview->text()));
    // Any other preset leaves no use for a scale of its own, which follows
    // again and shows that preset's share.
    preset->setCurrentIndex(int(ResolutionPreset::Quality));
    QCOMPARE(state(QStringLiteral("Percentage")), Follows);
    QCOMPARE(scale->value(), qRound(KWin::resolutionRatio(ResolutionPreset::Quality, 0) * 10000));
    QVERIFY2(preview->text().contains(rendered(ResolutionPreset::Quality, 0)), qPrintable(preview->text()));
    // The button beside the preset makes it follow the global one again.
    presetReset->click();
    QCOMPARE(preset->currentText(), QStringLiteral("Balanced"));
    QCOMPARE(state(QStringLiteral("Resolution")), Follows);

    // Nothing is greyed out by a switch being off: the strength can be set
    // before the sharpening that uses it.
    QVERIFY(!sharpening->isChecked());
    QCOMPARE(state(QStringLiteral("Sharpening")), Follows);
    QVERIFY(strength->isEnabled());

    // A limit above every screen leaves the game alone, and one too large to
    // count is held at the largest a limit can be rather than wrapping round.
    minimum->setCurrentText(QStringLiteral("7680x4320"));
    QCOMPARE(state(QStringLiteral("MinimumPixels")), States);
    QVERIFY2(preview->text().contains(QStringLiteral("not upscaled")), qPrintable(preview->text()));
    minimum->setCurrentText(QStringLiteral("999999x999999"));
    QCOMPARE(KWin::upscaleResolutionPixels(minimum, -1), std::numeric_limits<int>::max());

    // Defaults restore the global settings while the game is shown. What it
    // follows changes at once; what it states stays its own.
    module.defaults();
    QCOMPARE(preset->currentText(), QStringLiteral("Quality"));
    QCOMPARE(state(QStringLiteral("Resolution")), Follows);
    QCOMPARE(state(QStringLiteral("MinimumPixels")), States);
}

// A game's method follows the package's measurement where there is one and
// the global method otherwise, and is marked like its other settings: since
// Auto exists there is a sensible answer to inherit.
void ApplicationListTest::aGameInheritsItsMethods()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *all = module.widget()->findChild<QTabWidget *>(QStringLiteral("allApplications"));
    auto *game = editor->findChild<QTabWidget *>(QStringLiteral("applicationDetails"));
    QVERIFY(list && all && game);
    auto *globalX11 = all->findChild<QComboBox *>(QStringLiteral("method3"));
    auto *wayland = game->findChild<QComboBox *>(QStringLiteral("method0"));
    auto *x11 = game->findChild<QComboBox *>(QStringLiteral("method3"));
    auto *waylandReset = game->findChild<QToolButton *>(QStringLiteral("method0Reset"));
    QVERIFY(globalX11 && wayland && x11 && waylandReset);
    const auto kart = [list]() {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->text() == QStringLiteral("SuperTuxKart")) {
                return row;
            }
        }
        return -1;
    }();
    QVERIFY(kart > 0);
    list->setCurrentRow(kart);
    // The measured slot shows the package's measurement, inherited; an
    // unmeasured one the global method, Automatic by default.
    QCOMPARE(wayland->currentText(), KWin::upscaleMethodLabel(KWin::UpscaleMethod::AdvertisedMode));
    QCOMPARE(inheritance(game, QStringLiteral("method0")), Follows);
    QCOMPARE(x11->currentText(), KWin::upscaleMethodLabel(KWin::UpscaleMethod::Auto));
    QCOMPARE(inheritance(game, QStringLiteral("method3")), Follows);
    // The global method it follows is the one the page shows, applied or not.
    list->setCurrentRow(0);
    globalX11->setCurrentIndex(globalX11->findText(KWin::upscaleMethodLabel(KWin::UpscaleMethod::Off)));
    list->setCurrentRow(kart);
    QCOMPARE(x11->currentText(), KWin::upscaleMethodLabel(KWin::UpscaleMethod::Off));
    QCOMPARE(inheritance(game, QStringLiteral("method3")), Follows);
    // Choosing a method states it, and it is stored as the user's own.
    wayland->setCurrentIndex(wayland->findText(KWin::upscaleMethodLabel(KWin::UpscaleMethod::Auto)));
    QCOMPARE(inheritance(game, QStringLiteral("method0")), States);
    module.save();
    QFile stored(upscaleUserApplicationFile());
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QVERIFY(stored.readAll().contains("MethodWaylandFullScreen=Auto"));
    stored.close();
    // Resetting returns to the measurement, and stores nothing of its own.
    list->setCurrentRow(kart);
    waylandReset->click();
    QCOMPARE(wayland->currentText(), KWin::upscaleMethodLabel(KWin::UpscaleMethod::AdvertisedMode));
    QCOMPARE(inheritance(game, QStringLiteral("method0")), Follows);
    module.save();
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QVERIFY(!stored.readAll().contains("MethodWaylandFullScreen"));
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
