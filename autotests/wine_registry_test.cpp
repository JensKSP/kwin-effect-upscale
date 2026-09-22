/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineregistry.h"

#include <QTest>

namespace
{

// The head of a real Proton prefix's user.reg, shortened.
const QByteArray s_prefix = QByteArrayLiteral(
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

// Wine writes a blank line before every key and none after the last.
const QByteArray s_appended = QByteArrayLiteral(
    "\n"
    "[Software\\\\Wine\\\\Explorer] 1790000000\n"
    "\"Desktop\"=\"Default\"\n");

} // namespace

class WineRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void recognisesOnlyWinesOwnHeader();
    void addsAMissingKeyAtTheEndAndLeavesEverythingElse();
    void replacesAValueInPlace();
    void insertsAValueAfterTheKeysMetadata();
    void comparesNamesWithoutRegardToCase();
    void replacesEveryLineOfAContinuedValue();
    void readsOnlyPlainStrings();
    void removesTheValuesAndKeepsTheKeys();
    void refusesAnEmptySize();
};

void WineRegistryTest::recognisesOnlyWinesOwnHeader()
{
    QVERIFY(wineIsRegistry(s_prefix));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("REGEDIT4\n")));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 20\n")));
    QVERIFY(!wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2 beta\n")));
    QVERIFY(wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2\r\n")));
    QVERIFY(wineIsRegistry(QByteArrayLiteral("WINE REGISTRY Version 2")));
    QVERIFY(!wineWithVirtualDesktop(QByteArrayLiteral("[x]\n"), QSize(2560, 1440), 0));
    QVERIFY(!wineWithoutVirtualDesktop(QByteArray()));
}

void WineRegistryTest::addsAMissingKeyAtTheEndAndLeavesEverythingElse()
{
    const std::optional<QByteArray> result = wineWithVirtualDesktop(s_prefix, QSize(2560, 1440), 1790000000);
    QVERIFY(result);
    QByteArray expected = s_prefix;
    expected.insert(expected.indexOf("\"Other\""), "\"Default\"=\"2560x1440\"\n");
    expected.append(s_appended);
    QCOMPARE(*result, expected);
    QCOMPARE(wineDesktopValues(*result), (WineDesktopValues{.desktop = QStringLiteral("Default"), .defaultSize = QStringLiteral("2560x1440")}));
}

void WineRegistryTest::replacesAValueInPlace()
{
    const std::optional<QByteArray> first = wineWithVirtualDesktop(s_prefix, QSize(2560, 1440), 1790000000);
    QVERIFY(first);
    const std::optional<QByteArray> second = wineWithVirtualDesktop(*first, QSize(1920, 1080), 1790000001);
    QVERIFY(second);
    QByteArray expected = *first;
    expected.replace("2560x1440", "1920x1080");
    QCOMPARE(*second, expected);
}

void WineRegistryTest::insertsAValueAfterTheKeysMetadata()
{
    const QByteArray text = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "#time=1dc2b8f2a8c1e2a\n"
        "\"Other\"=\"1\"\n");
    const std::optional<QByteArray> result = wineWithVirtualDesktop(text, QSize(2560, 1440), 1790000000);
    QVERIFY(result);
    QVERIFY(result->startsWith(QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "#time=1dc2b8f2a8c1e2a\n"
        "\"Desktop\"=\"Default\"\n"
        "\"Other\"=\"1\"\n")));
}

void WineRegistryTest::comparesNamesWithoutRegardToCase()
{
    const QByteArray text = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[software\\\\wine\\\\explorer] 1789805658\n"
        "\"desktop\"=\"shell\"\n");
    QCOMPARE(wineDesktopValues(text).desktop, QStringLiteral("shell"));
    const std::optional<QByteArray> result = wineWithVirtualDesktop(text, QSize(2560, 1440), 1790000000);
    QVERIFY(result);
    QCOMPARE(result->count("[software\\\\wine\\\\explorer]"), 1);
    QCOMPARE(result->count("[Software\\\\Wine\\\\Explorer]"), 0);
    QVERIFY(!result->contains("shell"));
}

void WineRegistryTest::replacesEveryLineOfAContinuedValue()
{
    const QByteArray text = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "\"Desktop\"=hex:00,01,\\\n"
        "  02,03\n"
        "\"After\"=\"kept\"\n");
    QVERIFY(!wineDesktopValues(text).desktop);
    const std::optional<QByteArray> result = wineWithVirtualDesktop(text, QSize(2560, 1440), 1790000000);
    QVERIFY(result);
    QVERIFY(result->contains("\"Desktop\"=\"Default\"\n\"After\"=\"kept\"\n"));
    QVERIFY(!result->contains("02,03"));
}

void WineRegistryTest::readsOnlyPlainStrings()
{
    const QByteArray text = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "\"Desktop\"=str(2):\"%NAME%\"\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n"
        "\"Default\"=\"a\\\\b\\\"c\"\n");
    const WineDesktopValues values = wineDesktopValues(text);
    QVERIFY(!values.desktop);
    QCOMPARE(values.defaultSize, QStringLiteral("a\\b\"c"));
}

void WineRegistryTest::removesTheValuesAndKeepsTheKeys()
{
    const QByteArray withKeys = QByteArrayLiteral(
        "WINE REGISTRY Version 2\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer] 1789805658\n"
        "\n"
        "[Software\\\\Wine\\\\Explorer\\\\Desktops] 1789805658\n"
        "\"Other\"=\"800x600\"\n");
    const std::optional<QByteArray> set = wineWithVirtualDesktop(withKeys, QSize(2560, 1440), 1790000000);
    QVERIFY(set);
    const std::optional<QByteArray> removed = wineWithoutVirtualDesktop(*set);
    QVERIFY(removed);
    QCOMPARE(*removed, withKeys);
    QCOMPARE(wineDesktopValues(*removed), WineDesktopValues{});
    QCOMPARE(wineWithoutVirtualDesktop(s_prefix), s_prefix);
}

void WineRegistryTest::refusesAnEmptySize()
{
    QVERIFY(!wineWithVirtualDesktop(s_prefix, QSize(), 0));
    QVERIFY(!wineWithVirtualDesktop(s_prefix, QSize(0, 1440), 0));
}

QTEST_GUILESS_MAIN(WineRegistryTest)

#include "wine_registry_test.moc"
