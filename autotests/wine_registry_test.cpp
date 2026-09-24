/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineregistry.h"

#include "winedisplaymode.h"

#include <QTest>

namespace
{

// The head of a real Proton prefix's user.reg, shortened.
const QByteArray s_user = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000\n"
    "\n"
    "#arch=win64\n"
    "\n"
    "[Control Panel\\\\Desktop] 1789805658\n"
    "#time=1dc2b8f2a8c1e2a\n"
    "\"ActiveWndTrackTimeout\"=dword:00000000\n"
    "\n"
    "[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n"
    "\"Other\"=\"800x600\"\n");

// What a prefix's system.reg keeps of the devices it found the last time it ran,
// as a real one spells them, shortened.
const QByteArray s_machine = QByteArrayLiteral(
    "WINE REGISTRY Version 2\n"
    ";; All keys relative to \\\\Machine\n"
    "\n"
    "#arch=win64\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0000&0000] 1790154356\n"
    "#time=1dd4b3ac0fd3a58\n"
    "\"DeviceDesc\"=\"Generic Non-PnP Monitor\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0000&0000\\\\Device Parameters] 1790154356\n"
    "\"EDID\"=hex:00,ff\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Default_Monitor\\\\0001&0000] 1790154356\n"
    "\"DeviceDesc\"=\"Generic Non-PnP Monitor\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000] 1790154350\n"
    "\"DeviceDesc\"=\"AMD Radeon Graphics\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Enum\\\\PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000\\\\Device Parameters] 1790154350\n"
    "\"VideoID\"=\"{65aaded5-ba18-41e2-9572-7290231b645a}\"\n"
    "\n"
    "[System\\\\ControlSet001\\\\Hardware Profiles\\\\Current\\\\Software\\\\Fonts] 1790154351\n"
    "\"LogPixels\"=dword:00000060\n");

const QByteArray s_card = QByteArrayLiteral("PCI\\\\VEN_1002&DEV_1586&SUBSYS_00000000&REV_00\\\\00000000");
const QByteArray s_cardId = QByteArrayLiteral("{65aaded5-ba18-41e2-9572-7290231b645a}");
const QByteArray s_monitor = QByteArrayLiteral("DISPLAY\\\\Default_Monitor\\\\0000&0000");
const QByteArray s_secondMonitor = QByteArrayLiteral("DISPLAY\\\\Default_Monitor\\\\0001&0000");

// The screen a prepared program is on, at the size the effect wants, and the
// second output of the session beside it.
const QList<WineScreen> s_screens = {{.rect = QRect(0, 0, 2560, 1440), .rate = 60}, {.rect = QRect(3840, 0, 3840, 2160), .rate = 120}};
const QList<WineScreen> s_oneScreen = {{.rect = QRect(0, 0, 2560, 1440), .rate = 60}};

const QByteArray s_sourceKeyStart = QByteArrayLiteral(
    "[System\\\\ControlSet001\\\\Hardware Profiles\\\\Current\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\"
    "{65aaded5-ba18-41e2-9572-7290231b645a}\\\\");
const QByteArray s_sourceKey = s_sourceKeyStart + "0000]";

} // namespace

class WineRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void recognisesOnlyWinesOwnHeader();
    void findsTheDevicesThePrefixDescribed();
    void describesTheScreenWhereWineReadsIt();
    void saysWhatTheScreenOffers();
    void describesAnotherSizeInPlaceOfTheOneBefore();
    void takesTheDescriptionAwayAgain();
    void refusesWhatItCannotDescribe();
    void describesEveryScreenTheSessionHas();
    void readsTheUsersVirtualDesktop();
};

void WineRegistryTest::recognisesOnlyWinesOwnHeader()
{
    QVERIFY(wineIsRegistry(s_user));
    QVERIFY(wineIsRegistry(s_machine));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("REGEDIT4\n")));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 20\n")));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2 beta\n")));
    QVERIFY(wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2\r\n")));
    QVERIFY(wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2")));
    QVERIFY(!wineWithScreens(QByteArrayLiteral("[x]\n"), s_oneScreen, 0));
    QVERIFY(!wineWithoutScreen(QByteArray()));
}

void WineRegistryTest::findsTheDevicesThePrefixDescribed()
{
    const WineScreenDevices devices = wineScreenDevices(s_machine);
    QCOMPARE(devices.card, s_card);
    QCOMPARE(devices.cardId, s_cardId);
    QCOMPARE(devices.monitors, (QList<QByteArray>{s_monitor, s_secondMonitor}));
    QVERIFY(!devices.isEmpty());
    // A prefix that has not run yet describes nothing.
    QVERIFY(wineScreenDevices(s_user).isEmpty());
    QVERIFY(wineScreens(s_machine).isEmpty());
}

void WineRegistryTest::describesTheScreenWhereWineReadsIt()
{
    const std::optional<QByteArray> result = wineWithScreens(s_machine, s_oneScreen, 1790000000);
    QVERIFY(result);
    // Everything the prefix wrote itself stays as it was.
    QVERIFY(result->startsWith(s_machine));
    // The key that names the source, and the source key itself.
    QVERIFY(result->contains("[HARDWARE\\\\DEVICEMAP\\\\VIDEO] 1790000000\n"
                             "\"\\\\Device\\\\Video0\"=\"\\\\Registry\\\\Machine\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\"
                             "{65aaded5-ba18-41e2-9572-7290231b645a}\\\\0000\"\n"));
    QVERIFY(result->contains(QByteArray(s_sourceKey + " 1790000000\n")));
    QVERIFY(result->contains(QByteArray("\"GPUID\"=\"" + s_card + "\"\n")));
    QVERIFY(result->contains(QByteArray("\"MonitorID0\"=\"" + s_monitor + "\"\n")));
    // Attached to the desktop and the primary screen, or it has no size at all.
    QVERIFY(result->contains("\"StateFlags\"=dword:00000005\n"));
    // And it reads back as the screen it describes.
    QCOMPARE(wineScreens(*result), s_oneScreen);
}

void WineRegistryTest::saysWhatTheScreenOffers()
{
    const std::optional<QByteArray> result = wineWithScreens(s_machine, {{.rect = QRect(0, 0, 1920, 1080), .rate = 120}}, 1790000000);
    QVERIFY(result);
    // As many modes as the list holds, or Wine reads past its end.
    const int count = wineDisplayModeCount(wineDisplayModes(QSize(1920, 1080), 120));
    QVERIFY(result->contains(QByteArray("\"ModeCount\"=dword:" + QByteArray::number(count, 16).rightJustified(8, '0') + "\n")));
    QCOMPARE(result->count("\"Modes\"=hex:"), 1);
    // Broken into lines of Wine's own width, the last one without a backslash.
    const qsizetype modes = result->indexOf("\"Modes\"=hex:");
    const qsizetype end = result->indexOf("\n\"MonitorID0\"", modes);
    QVERIFY(end > modes);
    const QByteArray value = result->mid(modes, end - modes);
    QVERIFY(value.contains("\\\n  "));
    QVERIFY(!value.endsWith('\\'));
    for (const QByteArray &line : value.split('\n')) {
        QVERIFY(line.size() <= 77);
    }
}

void WineRegistryTest::describesAnotherSizeInPlaceOfTheOneBefore()
{
    const std::optional<QByteArray> first = wineWithScreens(s_machine, s_oneScreen, 1790000000);
    QVERIFY(first);
    const QList<WineScreen> other = {{.rect = QRect(0, 0, 1920, 1080), .rate = 60}};
    const std::optional<QByteArray> second = wineWithScreens(*first, other, 1790000001);
    QVERIFY(second);
    QCOMPARE(wineScreens(*second), other);
    QCOMPARE(second->count("[HARDWARE\\\\DEVICEMAP\\\\VIDEO]"), 1);
    QCOMPARE(second->count(s_sourceKey), 1);
    // The same again changes nothing but the time it was written.
    const std::optional<QByteArray> again = wineWithScreens(*second, other, 1790000001);
    QCOMPARE(again, second);
}

void WineRegistryTest::takesTheDescriptionAwayAgain()
{
    const std::optional<QByteArray> written = wineWithScreens(s_machine, s_oneScreen, 1790000000);
    QVERIFY(written);
    // Wine adds to the key while it runs; the whole key goes, with what it added.
    QByteArray used = *written;
    used.insert(used.indexOf("\"GPUID\""), "\"Depth\"=dword:00000020\n\"SymbolicLinkValue\"=hex(6):00\n");
    const std::optional<QByteArray> removed = wineWithoutScreen(used);
    QVERIFY(removed);
    QCOMPARE(*removed, s_machine);
    QVERIFY(wineScreens(*removed).isEmpty());
    // A prefix that has none is left as it is.
    QCOMPARE(wineWithoutScreen(s_machine), s_machine);
}

void WineRegistryTest::refusesWhatItCannotDescribe()
{
    QVERIFY(!wineWithScreens(s_machine, {}, 0));
    QVERIFY(!wineWithScreens(s_machine, {{.rect = QRect(), .rate = 60}}, 0));
    QVERIFY(!wineWithScreens(s_machine, {{.rect = QRect(0, 0, 0, 1440), .rate = 60}}, 0));
    // Without the devices of its own there is nothing to describe a screen for.
    QVERIFY(!wineWithScreens(s_user, s_oneScreen, 0));
    const QByteArray withoutMonitor = QByteArray(s_machine).replace("DISPLAY\\\\Default_Monitor", "DISPLAY\\\\Other_Monitor\\\\Deeper");
    QVERIFY(!wineWithScreens(withoutMonitor, s_oneScreen, 0));
}

// Every output of the session, so that a program does not see one screen where
// the session has two. One screen needs one monitor of the prefix's own, and
// screens beyond those are left out rather than described without one.
void WineRegistryTest::describesEveryScreenTheSessionHas()
{
    const std::optional<QByteArray> result = wineWithScreens(s_machine, s_screens, 1790000000);
    QVERIFY(result);
    QCOMPARE(wineScreens(*result), s_screens);
    // One key each, the program's own first, and the map naming both.
    QCOMPARE(result->count(QByteArray(s_sourceKeyStart + "0000]")), 1);
    QCOMPARE(result->count(QByteArray(s_sourceKeyStart + "0001]")), 1);
    QVERIFY(result->contains("\"\\\\Device\\\\Video0\"="));
    QVERIFY(result->contains("\"\\\\Device\\\\Video1\"="));
    // Each screen has a monitor of its own, or Wine gives it no size.
    QVERIFY(result->contains(QByteArray("\"MonitorID0\"=\"" + s_monitor + "\"\n")));
    QVERIFY(result->contains(QByteArray("\"MonitorID0\"=\"" + s_secondMonitor + "\"\n")));
    // The program's own screen is the primary one; the other is only attached.
    QCOMPARE(result->count("\"StateFlags\"=dword:00000005\n"), 1);
    QCOMPARE(result->count("\"StateFlags\"=dword:00000001\n"), 1);

    // A prefix that knows one monitor gets one screen described, not two.
    const QByteArray oneMonitor = QByteArray(s_machine).replace("DISPLAY\\\\Default_Monitor\\\\0001&0000", "DISPLAY\\\\Default_Monitor\\\\0001&0000\\\\Deeper");
    const std::optional<QByteArray> single = wineWithScreens(oneMonitor, s_screens, 1790000000);
    QVERIFY(single);
    QCOMPARE(wineScreens(*single), s_oneScreen);
}

void WineRegistryTest::readsTheUsersVirtualDesktop()
{
    const QByteArray text = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[software\\\\wine\\\\explorer] 1789805658\n"
        "\"desktop\"=\"shell\"\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n"
        "\"Default\"=\"a\\\\b\\\"c\"\n");
    const WineDesktopValues values = wineDesktopValues(text);
    QCOMPARE(values.desktop, QStringLiteral("shell"));
    QCOMPARE(values.defaultSize, QStringLiteral("a\\b\"c"));
    QCOMPARE(wineDesktopValues(s_user), (WineDesktopValues{.desktop = std::nullopt, .defaultSize = std::nullopt}));
    // Values of another kind than a plain string say nothing.
    const QByteArray other = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "\"Desktop\"=hex:00,01,\\\n"
        "  02,03\n");
    QVERIFY(!wineDesktopValues(other).desktop);
}

QTEST_GUILESS_MAIN(WineRegistryTest)

#include "wine_registry_test.moc"
