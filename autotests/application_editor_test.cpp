/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "applicationeditor.h"
#include "upscale_config.h"

#include "settings_fixture.h"

#include <KPluginMetaData>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusContext>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include <initializer_list>

// KWin picks the window itself and hands back its identity. Only this private
// bus answers here, and each case the settings page has to handle is selected
// by the reply this stands in for.
class TestWindowPicker : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")

public:
    enum Outcome {
        Identified,
        WithoutIdentity,
        Cancelled,
        Refused,
    };
    Outcome outcome = Identified;

public Q_SLOTS:
    QVariantMap queryWindowInfo()
    {
        if (outcome == Cancelled) {
            sendErrorReply(QStringLiteral("org.kde.KWin.Error.UserCancel"), QStringLiteral("Cancelled"));
            return {};
        }
        if (outcome == Refused) {
            sendErrorReply(QStringLiteral("org.kde.KWin.Error.InvalidWindow"), QStringLiteral("No such window"));
            return {};
        }
        if (outcome == WithoutIdentity) {
            return {{QStringLiteral("resourceClass"), QString()}, {QStringLiteral("resourceName"), QString()}};
        }
        return {{QStringLiteral("resourceClass"), QStringLiteral("hedgewars")},
                {QStringLiteral("resourceName"), QStringLiteral("hedgewars")}};
    }
};

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
    auto *method = editor->findChild<QComboBox *>(QStringLiteral("applicationMethod"));
    auto *preset = editor->findChild<QComboBox *>(QStringLiteral("applicationPreset"));
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
    QCOMPARE(windowClass->text(), QStringLiteral("supertuxkart"));
    QCOMPARE(program->text(), QStringLiteral("supertuxkart"));
    QCOMPARE(method->currentText(), KWin::describeControlMethod(KWin::UpscaleControlMethod::AdvertisedMode));
    QVERIFY(enabled->isChecked());
    QVERIFY(!note->text().isEmpty());
    // An entry this build ships comes back with the next package, so removing
    // it would not remove anything.
    QVERIFY(!remove->isEnabled());

    // A combo box reports a choice only when the user makes it, so the keyboard
    // drives it here rather than setCurrentIndex.
    const int automatic = preset->currentIndex();
    QTest::keyClick(preset, Qt::Key_Down);
    QVERIFY(preset->currentIndex() != automatic);
    const QString chosen = preset->currentText();
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
    QVERIFY2(stored.contains(QStringLiteral("Preset=")), qPrintable(stored));
    // Everything the user did not touch keeps following the installed package.
    QVERIFY2(!stored.contains(QStringLiteral("Method=")), qPrintable(stored));
    QVERIFY(!stored.contains(QStringLiteral("WindowClass=")));

    // Saving reloads, so the page shows what was stored rather than what was
    // typed, and the choice survives the round trip.
    list->setCurrentRow(kart);
    QCOMPARE(preset->currentText(), chosen);

    // The page says whether the list still follows the package.
    QLabel *summary = module.widget()->findChild<QLabel *>(QStringLiteral("applicationSummary"));
    QVERIFY(summary);
    QVERIFY2(summary->text().contains(QStringLiteral("differs")), qPrintable(summary->text()));
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

    // KWin reports the identity; the effect records it and asks for nothing
    // until the user says which program is behind it.
    detect->click();
    QCOMPARE(list->count(), shipped + 1);
    QCOMPARE(windowClass->text(), QStringLiteral("hedgewars"));
    QCOMPARE(instance->text(), QStringLiteral("hedgewars"));

    // A window that reports no identity cannot be recognized later, so it is
    // refused with an explanation rather than added as an entry matching
    // everything on the screen.
    picker.outcome = TestWindowPicker::WithoutIdentity;
    answerNextDialog(QMessageBox::Ok);
    detect->click();
    QCOMPARE(list->count(), shipped + 1);

    // Cancelling the selection is an ordinary outcome and says nothing.
    picker.outcome = TestWindowPicker::Cancelled;
    detect->click();
    QCOMPARE(list->count(), shipped + 1);

    // Any other failure is worth saying out loud.
    picker.outcome = TestWindowPicker::Refused;
    answerNextDialog(QMessageBox::Ok);
    detect->click();
    QCOMPARE(list->count(), shipped + 1);

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
    QVERIFY2(summary->text().contains(QStringLiteral("follows every update")), qPrintable(summary->text()));

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
    QVERIFY2(summary->text().contains(QStringLiteral("follows every update")), qPrintable(summary->text()));
    // Restoring must leave the shipped entries reachable rather than suppress
    // them, so the page can still read them afterwards.
    QVERIFY(list->count() > 1);
}

int main(int argc, char **argv)
{
    UpscaleApplicationEditorTest test;
    return runSettingsTest(&test, argc, argv);
}

#include "application_editor_test.moc"
