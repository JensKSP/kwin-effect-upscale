/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The identity fields of the profile editor, and what they say about the open
// windows an entry would match. A program of its own, because each settings
// test brings up a session bus of its own, on which a stand-in answers for the
// effect here, and a process keeps the first bus it connected to.

#include "application.h"
#include "identitycontrols.h"

#include "editor_stand_ins.h"
#include "settings_fixture.h"

#include <QDBusConnection>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTest>
#include <QWidget>

class IdentityControlsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void saysWhichOpenWindowsAnEntryMatches();
};

void IdentityControlsTest::saysWhichOpenWindowsAnEntryMatches()
{
    QWidget host;
    auto *form = new QFormLayout(&host);
    KWin::UpscaleIdentityControls controls;
    controls.build(form, &host);
    auto *matches = host.findChild<QLabel *>(QStringLiteral("applicationMatches"));
    auto *program = host.findChild<QLineEdit *>(QStringLiteral("applicationProgram"));
    QVERIFY(matches && program);
    KWin::UpscaleApplication kart;
    kart.executable = QStringLiteral(".*/supertuxkart");
    kart.executableMatch = KWin::UpscaleStringMatch::RegularExpression;

    // Nobody answers without the effect, and no answer is not "no window",
    // so nothing is said.
    controls.show(kart);
    QTest::qWait(600);
    QVERIFY(matches->isHidden());

    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin")));
    TestProgramLookup effect;
    effect.windows = {QStringLiteral("SuperTuxKart")};
    QVERIFY(bus.registerObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"), &effect, QDBusConnection::ExportAllSlots));
    controls.show(kart);
    QTRY_COMPARE(matches->text(), QStringLiteral("Matches the open window “SuperTuxKart”."));
    QVERIFY(!matches->isHidden());
    // The entry is described in the file's own terms, so an entry not stored
    // yet can be asked about.
    QCOMPARE(effect.entry.value(QStringLiteral("Executable")).toString(), QStringLiteral(".*/supertuxkart"));
    QCOMPARE(effect.entry.value(QStringLiteral("ExecutableMatch")).toString(), QStringLiteral("RegularExpression"));
    QCOMPARE(effect.entry.value(QStringLiteral("WindowClassMatch")).toString(), QStringLiteral("Exact"));

    // Asked again once typing stops.
    effect.windows.clear();
    QTest::keyClicks(program, QStringLiteral("x"));
    QTRY_COMPARE(matches->text(), QStringLiteral("No open window matches."));
    QCOMPARE(effect.entry.value(QStringLiteral("Executable")).toString(), QStringLiteral(".*/supertuxkartx"));

    // With no entry to describe there is nothing to say.
    controls.setEnabled(false);
    QVERIFY(matches->isHidden());

    bus.unregisterObject(QStringLiteral("/org/kde/KWin/Effect/Upscale1"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin")));
}

int main(int argc, char **argv)
{
    return runSettingsTest<IdentityControlsTest>(argc, argv);
}

#include "identity_controls_test.moc"
