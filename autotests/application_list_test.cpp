/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The application list as a whole: "All applications" pinned first, the
// defaults of System Settings reaching it and nothing else, and the list
// leaving the page as a file and coming back.

#include "application.h"
#include "applicationeditor.h"
#include "upscale_config.h"
#include "upscaleconfig.h"

#include "settings_fixture.h"

#include <KPluginMetaData>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <ranges>

class ApplicationListTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void showsTheGlobalSettingsAsTheFirstEntry();
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
    // Pinned first, and not a switch of its own: whether the global profile
    // acts is the labelled check box in its panel.
    QCOMPARE(list->item(0)->text(), QStringLiteral("All applications"));
    QVERIFY(!(list->item(0)->flags() & Qt::ItemIsUserCheckable));
    list->setCurrentRow(0);
    QCOMPARE(details->currentWidget(), all);
    QVERIFY(!remove->isEnabled() && !up->isEnabled() && !down->isEnabled());
    // Every other row shows a game's tabs instead.
    list->setCurrentRow(1);
    QVERIFY(details->currentWidget() != all);
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
    auto *control = module.widget()->findChild<QCheckBox *>(QStringLiteral("resolutionControl"));
    QVERIFY(list && preset && control);
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    add->click();
    const int entries = list->count();
    preset->setCurrentIndex(0);
    control->setChecked(false);
    module.defaults();
    QCOMPARE(list->count(), entries);
    // Defaults put the generated configuration back to upscaleconfig.kcfg's
    // values, which the page then shows.
    QCOMPARE(preset->currentIndex(), KWin::UpscaleConfig::resolution());
    QVERIFY(preset->currentIndex() != 0);
    QVERIFY(control->isChecked());
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
