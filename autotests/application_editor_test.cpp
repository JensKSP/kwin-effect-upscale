/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"
#include "applicationeditor.h"
#include "identitycontrols.h"
#include "matching.h"
#include "methodcontrols.h"
#include "upscale_config.h"

#include "editor_stand_ins.h"
#include "settings_fixture.h"

#include <KPluginMetaData>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusPendingCallWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <initializer_list>

// The page's confirmations and warnings are modal, and a test that waited for
// a person would hang rather than fail. This answers the next one that opens.
static void answerNextDialog(QMessageBox::StandardButton button)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, button, attempts = 0]() mutable {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        // Bounded, so that a dialog which never opens ends the wait rather than
        // leaving a timer running for the rest of the test run.
        if (!box && ++attempts < 400) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        if (!box) {
            return;
        }
        if (QAbstractButton *target = box->button(button)) {
            target->click();
        } else {
            box->reject();
        }
    });
    timer->start();
}

class UpscaleApplicationEditorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void editsTheApplicationList();
    void addsApplicationsAndRemovesOnlyItsOwn();
    void addsAnApplicationFromAWindow();
    void restoresTheShippedApplicationList();
    void givesEveryPendingApplicationItsOwnIdentifier();
    void refusesAnEntryNothingCouldEverMatch();
    void resetDiscardsThePendingApplicationEdits();
    void reordersTheMatchingOrder();

private:
    static QString userConfig();
};

QString UpscaleApplicationEditorTest::userConfig()
{
    QFile file(upscaleUserApplicationFile());
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// Each test starts from the list this build ships, with no user layer at all.
void UpscaleApplicationEditorTest::init()
{
    QFile::remove(upscaleUserApplicationFile());
    KWin::upscaleReloadApplications();
}

// Editing is held until the page is applied, as the rest of the page behaves,
// and what reaches the file is only what the user actually changed.
void UpscaleApplicationEditorTest::editsTheApplicationList()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *windowClass = editor->findChild<QLineEdit *>(QStringLiteral("applicationWindowClass"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *program = editor->findChild<QLineEdit *>(QStringLiteral("applicationProgram"));
    auto *method = editor->findChild<QComboBox *>(QStringLiteral("method0"));
    // Built from the settings table, so each control is named by the key it
    // stores rather than by a name chosen per field.
    auto *preset = editor->findChild<QComboBox *>(QStringLiteral("Resolution"));
    auto *enabled = editor->findChild<QCheckBox *>(QStringLiteral("applicationEnabled"));
    auto *note = editor->findChild<QLabel *>(QStringLiteral("applicationNote"));
    auto *remove = editor->findChild<QPushButton *>(QStringLiteral("applicationRemove"));
    for (const QWidget *widget : std::initializer_list<const QWidget *>{list, name, windowClass, instance, program,
                                                                        method, preset, enabled, note, remove}) {
        QVERIFY(widget);
    }
    // The shipped catalogue, read from the file this build installs.
    QVERIFY(list->count() > 1);

    const int kart = [list]() {
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->text() == QStringLiteral("SuperTuxKart")) {
                return row;
            }
        }
        return -1;
    }();
    QVERIFY(kart >= 0);
    list->setCurrentRow(kart);
    // Selecting an entry shows what it says, including the description that
    // records why it looks the way it does.
    QCOMPARE(name->text(), QStringLiteral("SuperTuxKart"));
    // It states its program alone, as the file name in any folder, because
    // its method is said before the window exists.
    QVERIFY(windowClass->text().isEmpty());
    QCOMPARE(program->text(), QStringLiteral(".*/supertuxkart"));
    auto *programMatch = editor->findChild<QComboBox *>(QStringLiteral("applicationProgramMatch"));
    QVERIFY(programMatch);
    QCOMPARE(programMatch->currentIndex(), int(KWin::UpscaleStringMatch::RegularExpression));
    QCOMPARE(method->currentText(), KWin::upscaleMethodLabel(KWin::UpscaleMethod::AdvertisedMode));
    QVERIFY(enabled->isChecked());
    QVERIFY(!note->text().isEmpty());
    // An entry this build ships comes back with the next package, so removing
    // it would not remove anything.
    QVERIFY(!remove->isEnabled());

    // A combo box reports a choice only when the user makes it, so the keyboard
    // drives it here rather than setCurrentIndex.
    // A profile that states nothing shows "use global" first, naming the value
    // it follows, so that following it is a choice made knowingly.
    QCOMPARE(preset->currentIndex(), 0);
    QVERIFY2(preset->currentText().contains(QStringLiteral("Quality")), qPrintable(preset->currentText()));
    const int inherited = preset->currentIndex();
    QTest::keyClick(preset, Qt::Key_Down);
    QVERIFY(preset->currentIndex() != inherited);
    const QString chosen = preset->currentText();
    auto *minimum = editor->findChild<QSpinBox *>(QStringLiteral("MinimumPixels"));
    QVERIFY(minimum);
    // One step below the range is "use global", which the spin box names.
    QCOMPARE(minimum->value(), -1);
    minimum->setValue(2073600);
    // Nothing is written before the page is applied.
    QVERIFY(!userConfig().contains(QStringLiteral("Application-supertuxkart")));

    // The check box in the list and the one beside the details are the same
    // state, so each has to follow the other.
    list->item(kart)->setCheckState(Qt::Unchecked);
    QVERIFY(!enabled->isChecked());
    enabled->click();
    QCOMPARE(list->item(kart)->checkState(), Qt::Checked);

    module.save();
    const QString stored = userConfig();
    QVERIFY2(stored.contains(QStringLiteral("[Application-supertuxkart]")), qPrintable(stored));
    QVERIFY2(stored.contains(QStringLiteral("Resolution=")), qPrintable(stored));
    // Everything the user did not touch keeps following the installed package,
    // including all six measured answers.
    QVERIFY2(!stored.contains(QStringLiteral("Method")), qPrintable(stored));
    QVERIFY(!stored.contains(QStringLiteral("WindowClass=")));

    // Saving reloads, so the page shows what was stored rather than what was
    // typed, and the choice survives the round trip.
    list->setCurrentRow(kart);
    QCOMPARE(preset->currentText(), chosen);
    QCOMPARE(minimum->value(), 2073600);

    // The page says whether the list still follows the package.
    QLabel *summary = module.widget()->findChild<QLabel *>(QStringLiteral("applicationSummary"));
    QVERIFY(summary);
    QVERIFY2(summary->text().contains(QStringLiteral("your changes")), qPrintable(summary->text()));
}

void UpscaleApplicationEditorTest::addsApplicationsAndRemovesOnlyItsOwn()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *program = editor->findChild<QLineEdit *>(QStringLiteral("applicationProgram"));
    auto *note = editor->findChild<QLabel *>(QStringLiteral("applicationNote"));
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    auto *remove = editor->findChild<QPushButton *>(QStringLiteral("applicationRemove"));
    QVERIFY(list && name && instance && program && note && add && remove);
    const int shipped = list->count();

    add->click();
    QCOMPARE(list->count(), shipped + 1);
    QCOMPARE(list->currentRow(), shipped);
    // A line edit reports an edit only when the user makes it.
    name->clear();
    QTest::keyClicks(name, QStringLiteral("Hedgewars"));
    QTest::keyClicks(instance, QStringLiteral("hedgewars"));
    QTest::keyClicks(program, QStringLiteral("hedgewars"));
    // An application the user added is marked as theirs and says what nobody
    // measured about it, rather than borrowing a shipped entry's description.
    QVERIFY2(list->item(shipped)->text().contains(QStringLiteral("Hedgewars")), qPrintable(list->item(shipped)->text()));
    QVERIFY2(note->text().contains(QStringLiteral("Added by you")), qPrintable(note->text()));
    // Theirs to remove, unlike the ones this build ships.
    QVERIFY(remove->isEnabled());

    module.save();
    QVERIFY2(userConfig().contains(QStringLiteral("Instance=hedgewars")), qPrintable(userConfig()));
    QVERIFY2(userConfig().contains(QStringLiteral("Executable=hedgewars")), qPrintable(userConfig()));
    QCOMPARE(list->count(), shipped + 1);

    // Removing takes effect on the file only once the page is applied, the
    // same as every other edit here.
    list->setCurrentRow(shipped);
    remove->click();
    QCOMPARE(list->count(), shipped);
    QVERIFY(userConfig().contains(QStringLiteral("Instance=hedgewars")));
    module.save();
    QVERIFY2(!userConfig().contains(QStringLiteral("Instance=hedgewars")), qPrintable(userConfig()));
    QCOMPARE(list->count(), shipped);
}

void UpscaleApplicationEditorTest::addsAnApplicationFromAWindow()
{
    TestWindowPicker picker;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin")));
    QVERIFY(bus.registerObject(QStringLiteral("/KWin"), &picker, QDBusConnection::ExportAllSlots));

    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *windowClass = editor->findChild<QLineEdit *>(QStringLiteral("applicationWindowClass"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *detect = editor->findChild<QPushButton *>(QStringLiteral("applicationAddFromWindow"));
    QVERIFY(list && windowClass && instance && detect);
    const int shipped = list->count();

    // KWin reports the window's identity. The effect is not loaded, so nobody
    // says which program is behind it, and the identity is what the entry
    // states.
    detect->click();
    QTRY_COMPARE(list->count(), shipped + 1);
    QCOMPARE(windowClass->text(), QStringLiteral("hedgewars"));
    QCOMPARE(instance->text(), QStringLiteral("hedgewars"));

    // A window that reports no identity cannot be recognized later, so it is
    // refused with an explanation rather than added as an entry matching
    // everything on the screen.
    picker.outcome = TestWindowPicker::WithoutIdentity;
    answerNextDialog(QMessageBox::Ok);
    detect->click();
    QTRY_VERIFY(editor->findChildren<QDBusPendingCallWatcher *>().isEmpty());
    QTRY_COMPARE(list->count(), shipped + 1);

    // Cancelling the selection is an ordinary outcome and says nothing.
    picker.outcome = TestWindowPicker::Cancelled;
    detect->click();
    QTRY_VERIFY(editor->findChildren<QDBusPendingCallWatcher *>().isEmpty());
    QTRY_COMPARE(list->count(), shipped + 1);

    // Any other failure is worth saying out loud.
    picker.outcome = TestWindowPicker::Refused;
    answerNextDialog(QMessageBox::Ok);
    detect->click();
    QTRY_VERIFY(editor->findChildren<QDBusPendingCallWatcher *>().isEmpty());
    QTRY_COMPARE(list->count(), shipped + 1);

    // With the effect answering, the entry states the exact path of this
    // copy, which is what finds it before its window exists, and nothing else.
    TestProgramLookup lookup;
    lookup.path = QStringLiteral("/usr/games/hedgewars");
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"), &lookup, QDBusConnection::ExportAllSlots));
    auto *program = editor->findChild<QLineEdit *>(QStringLiteral("applicationProgram"));
    QVERIFY(program);
    picker.outcome = TestWindowPicker::Identified;
    detect->click();
    QTRY_COMPARE(list->count(), shipped + 2);
    QCOMPARE(lookup.askedFor, QStringLiteral("{0b4a6c3e-8f0e-4a55-9d2c-1d1f5e3b7a90}"));
    QCOMPARE(program->text(), QStringLiteral("/usr/games/hedgewars"));
    QVERIFY(windowClass->text().isEmpty() && instance->text().isEmpty());

    // A runtime many games share names none of them, so the window's
    // identity is stated instead of its path.
    lookup.path = QStringLiteral("/steam/common/Proton 9.0/files/bin/wine64-preloader");
    detect->click();
    QTRY_COMPARE(list->count(), shipped + 3);
    QVERIFY(program->text().isEmpty());
    QCOMPARE(windowClass->text(), QStringLiteral("hedgewars"));
    QVERIFY(!KWin::upscaleIdentifiesOneProgram(QStringLiteral("/usr/bin/python3.12")));
    QVERIFY(!KWin::upscaleIdentifiesOneProgram(QString()));
    bus.unregisterObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"));

    bus.unregisterObject(QStringLiteral("/KWin"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin")));
}

// Restoring the list is a different file from the one Apply writes, and is not
// recoverable, so it asks first and then does it at once.
void UpscaleApplicationEditorTest::restoresTheShippedApplicationList()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    auto *summary = module.widget()->findChild<QLabel *>(QStringLiteral("applicationSummary"));
    auto *reset = module.widget()->findChild<QPushButton *>(QStringLiteral("resetApplications"));
    QVERIFY(editor && summary && reset);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    QVERIFY(list && add && name && instance);

    // Nothing to restore while the list is the one this build ships.
    QVERIFY(!KWin::UpscaleApplicationEditor::customized());
    QVERIFY(!reset->isEnabled());
    QVERIFY2(summary->text().contains(QStringLiteral("Default list")), qPrintable(summary->text()));

    const int shipped = list->count();
    add->click();
    name->clear();
    QTest::keyClicks(name, QStringLiteral("Mine"));
    QTest::keyClicks(instance, QStringLiteral("mine"));
    module.save();
    QVERIFY(KWin::UpscaleApplicationEditor::customized());
    QVERIFY(reset->isEnabled());
    QCOMPARE(list->count(), shipped + 1);

    // Answering no leaves the list exactly as it was.
    answerNextDialog(QMessageBox::No);
    reset->click();
    QCOMPARE(list->count(), shipped + 1);
    QVERIFY(KWin::UpscaleApplicationEditor::customized());

    answerNextDialog(QMessageBox::Yes);
    reset->click();
    QCOMPARE(list->count(), shipped);
    QVERIFY(!KWin::UpscaleApplicationEditor::customized());
    QVERIFY2(summary->text().contains(QStringLiteral("Default list")), qPrintable(summary->text()));
    // Restoring must leave the shipped entries reachable rather than suppress
    // them, so the page can still read them afterwards.
    QVERIFY(list->count() > 1);
}

// Two entries sharing an identifier would share a configuration group, and
// applying them would write one over the other.
void UpscaleApplicationEditorTest::givesEveryPendingApplicationItsOwnIdentifier()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    QVERIFY(list && name && instance && add);
    const int shipped = list->count();

    // Both added before anything is applied, so neither is stored yet and the
    // stored list alone cannot tell them apart.
    add->click();
    name->clear();
    QTest::keyClicks(name, QStringLiteral("First"));
    QTest::keyClicks(instance, QStringLiteral("first"));
    add->click();
    name->clear();
    QTest::keyClicks(name, QStringLiteral("Second"));
    QTest::keyClicks(instance, QStringLiteral("second"));
    QCOMPARE(list->count(), shipped + 2);

    module.save();
    QCOMPARE(list->count(), shipped + 2);
    const QString stored = userConfig();
    QVERIFY2(stored.contains(QStringLiteral("Instance=first")), qPrintable(stored));
    QVERIFY2(stored.contains(QStringLiteral("Instance=second")), qPrintable(stored));
    QVERIFY(KWin::upscaleApplicationFor({QString(), QString(), QStringLiteral("first")}));
    QVERIFY(KWin::upscaleApplicationFor({QString(), QString(), QStringLiteral("second")}));
}

// An entry stating no identity would match every window on the screen, so the
// reader drops it. Writing it would make it vanish without saying why.
void UpscaleApplicationEditorTest::refusesAnEntryNothingCouldEverMatch()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *program = editor->findChild<QLineEdit *>(QStringLiteral("applicationProgram"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    QVERIFY(list && name && program && instance && add);
    const int shipped = list->count();

    add->click();
    name->clear();
    QTest::keyClicks(name, QStringLiteral("Nameless"));
    // A name is not an identity: with no program, window class or instance
    // the entry would match every window.
    answerNextDialog(QMessageBox::Ok);
    module.save();
    // Kept here rather than written and then silently dropped, and the page
    // stays applicable so the user can correct it.
    QCOMPARE(list->count(), shipped + 1);
    QCOMPARE(list->currentRow(), shipped);
    QVERIFY2(!userConfig().contains(QStringLiteral("Nameless")), qPrintable(userConfig()));
    QVERIFY(module.needsSave());

    // A pattern that cannot be used is refused the same way, and the entry
    // says why where it is being edited.
    auto *programMatch = editor->findChild<QComboBox *>(QStringLiteral("applicationProgramMatch"));
    auto *note = editor->findChild<QLabel *>(QStringLiteral("applicationNote"));
    QVERIFY(programMatch && note);
    QTest::keyClicks(program, QStringLiteral("(nameless"));
    programMatch->setCurrentIndex(int(KWin::UpscaleStringMatch::RegularExpression));
    Q_EMIT programMatch->activated(programMatch->currentIndex());
    QVERIFY2(note->text().contains(QStringLiteral("(nameless")), qPrintable(note->text()));
    answerNextDialog(QMessageBox::Ok);
    module.save();
    QVERIFY2(!userConfig().contains(QStringLiteral("Nameless")), qPrintable(userConfig()));
    QVERIFY(module.needsSave());

    // A program alone is an identity: it finds the program before and after
    // its window exists.
    program->clear();
    QTest::keyClicks(program, QStringLiteral(".*/nameless"));
    module.save();
    QVERIFY2(userConfig().contains(QStringLiteral("Executable=.*/nameless")), qPrintable(userConfig()));
    QVERIFY(!module.needsSave());
}

// Reset discards the pending application edits with everything else on the
// page: leaving them would let a later Apply write what was just discarded.
void UpscaleApplicationEditorTest::resetDiscardsThePendingApplicationEdits()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("applicationName"));
    auto *instance = editor->findChild<QLineEdit *>(QStringLiteral("applicationInstance"));
    auto *add = editor->findChild<QPushButton *>(QStringLiteral("applicationAdd"));
    QVERIFY(list && name && instance && add);
    const int shipped = list->count();

    add->click();
    name->clear();
    QTest::keyClicks(name, QStringLiteral("Discarded"));
    QTest::keyClicks(instance, QStringLiteral("discarded"));
    QCOMPARE(list->count(), shipped + 1);

    module.load();
    QCOMPARE(list->count(), shipped);

    // And a later Apply must not bring it back.
    module.save();
    QVERIFY2(!userConfig().contains(QStringLiteral("discarded")), qPrintable(userConfig()));
    QVERIFY(!KWin::upscaleApplicationFor({QString(), QString(), QStringLiteral("discarded")}));
}

// The list's order is the matching order, so moving an entry is what lets a
// narrow entry win over a broad one. Only the two entries that changed
// places are stored as changed.
void UpscaleApplicationEditorTest::reordersTheMatchingOrder()
{
    QWidget host;
    KWin::UpscaleEffectConfig module(&host, KPluginMetaData());
    auto *editor = module.widget()->findChild<KWin::UpscaleApplicationEditor *>();
    QVERIFY(editor);
    auto *list = editor->findChild<QListWidget *>(QStringLiteral("applicationList"));
    auto *up = editor->findChild<QPushButton *>(QStringLiteral("applicationMoveUp"));
    auto *down = editor->findChild<QPushButton *>(QStringLiteral("applicationMoveDown"));
    QVERIFY(list && up && down && list->count() > 2);
    const QString first = list->item(0)->text();
    const QString second = list->item(1)->text();
    list->setCurrentRow(0);
    QVERIFY(!up->isEnabled());
    QVERIFY(down->isEnabled());

    down->click();
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(list->item(0)->text(), second);
    QCOMPARE(list->item(1)->text(), first);
    QVERIFY(module.needsSave());
    module.save();
    const std::vector<KWin::UpscaleApplication> &stored = KWin::upscaleApplications();
    QCOMPARE(stored[0].name, second);
    QCOMPARE(stored[1].name, first);
    // Two groups carry an Order now, and nothing else was written.
    QCOMPARE(userConfig().count(QStringLiteral("Order=")), 2);
    QVERIFY2(!userConfig().contains(QStringLiteral("Name=")), qPrintable(userConfig()));

    list->setCurrentRow(list->count() - 1);
    QVERIFY(!down->isEnabled());
    QVERIFY(up->isEnabled());
}

int main(int argc, char **argv)
{
    return runSettingsTest<UpscaleApplicationEditorTest>(argc, argv);
}

#include "application_editor_test.moc"
