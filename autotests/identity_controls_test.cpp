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
#include "pattern.h"

#include "editor_stand_ins.h"
#include "settings_fixture.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
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
    void storesAProgramPortably_data();
    void storesAProgramPortably();
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
    // The bus answers in order: once it has answered this, it has refused the
    // question the controls asked before it, and the refusal is delivered.
    QVERIFY(QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus.Peer"), QStringLiteral("Ping"))).type() == QDBusMessage::ReplyMessage);
    QCoreApplication::processEvents();
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

// What Add from Window stores for a program: the same game wherever it is
// installed, for another user and in another library, and never the home or the
// library it was found in.
void IdentityControlsTest::storesAProgramPortably_data()
{
    QTest::addColumn<QString>("found");
    QTest::addColumn<QString>("stored");
    QTest::addColumn<QString>("elsewhere");
    QTest::newRow("steam") << QStringLiteral("/home/jens/.local/share/Steam/steamapps/common/Left 4 Dead 2/hl2_linux")
                           << QStringLiteral(".*/Left 4 Dead 2/hl2_linux")
                           << QStringLiteral("/home/kim/.steam/debian-installation/steamapps/common/Left 4 Dead 2/hl2_linux");
    QTest::newRow("older steam library") << QStringLiteral("/mnt/games/SteamLibrary/SteamApps/common/Half-Life 2/hl2_linux")
                                         << QStringLiteral(".*/Half-Life 2/hl2_linux")
                                         << QStringLiteral("/data/SteamLibrary/steamapps/common/Half-Life 2/hl2_linux");
    QTest::newRow("installed") << QStringLiteral("/usr/games/supertuxkart") << QStringLiteral(".*/supertuxkart")
                               << QStringLiteral("/usr/local/bin/supertuxkart");
    QTest::newRow("in a home") << QStringLiteral("/home/jens/Games/Foo (Linux)/bin/foo.x86_64") << QStringLiteral(".*/foo\\.x86_64")
                               << QStringLiteral("/opt/foo/foo.x86_64");
}

void IdentityControlsTest::storesAProgramPortably()
{
    QFETCH(QString, found);
    QFETCH(QString, stored);
    QFETCH(QString, elsewhere);
    QCOMPARE(KWin::upscalePortableExecutable(found), stored);
    const KWin::UpscalePattern pattern(stored, KWin::UpscaleStringMatch::RegularExpression);
    QVERIFY(pattern.matches(found));
    QVERIFY(pattern.matches(elsewhere));
    QVERIFY(!pattern.matches(found + QStringLiteral("_old")));
    QVERIFY(!stored.contains(QStringLiteral("jens")) && !stored.contains(QStringLiteral("Steam")));
}

int main(int argc, char **argv)
{
    return runSettingsTest<IdentityControlsTest>(argc, argv);
}

#include "identity_controls_test.moc"
