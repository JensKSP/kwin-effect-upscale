/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedisplaymode.h"

#include <QSet>
#include <QTest>
#include <QtEndian>

namespace
{

// The record and the members this test reads, as win32u lays them out.
constexpr qsizetype s_recordSize = 220;
constexpr qsizetype s_sizeAt = 68;
constexpr qsizetype s_driverExtraAt = 70;
constexpr qsizetype s_depthAt = 168;
constexpr qsizetype s_widthAt = 172;
constexpr qsizetype s_heightAt = 176;
constexpr qsizetype s_rateAt = 184;

quint32 number(const QByteArray &modes, qsizetype index, qsizetype at)
{
    return qFromLittleEndian<quint32>(modes.constData() + index * s_recordSize + at);
}

quint16 shortNumber(const QByteArray &modes, qsizetype index, qsizetype at)
{
    return qFromLittleEndian<quint16>(modes.constData() + index * s_recordSize + at);
}

QSize sizeOf(const QByteArray &modes, qsizetype index)
{
    return QSize(static_cast<int>(number(modes, index, s_widthAt)), static_cast<int>(number(modes, index, s_heightAt)));
}

} // namespace

class WineDisplayModeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void offersThatSizeAndNoOther();
    void everyRecordIsOneWineCanRead();
    void offersSixtyHertzAndTheScreensOwnRate();
    void tellsTheCurrentModeItsSizeAndPlace();
    void refusesAnEmptySize();
};

void WineDisplayModeTest::offersThatSizeAndNoOther()
{
    const QByteArray modes = wineDisplayModes(QSize(2560, 1440), 60);
    const int count = wineDisplayModeCount(modes);
    QCOMPARE(modes.size(), count * s_recordSize);
    // Three colour depths of the one size at the one rate, and nothing else: a
    // program that picks from this list has only one size to pick.
    QCOMPARE(count, 3);
    QSet<QSize> sizes;
    for (qsizetype index = 0; index < count; ++index) {
        sizes.insert(sizeOf(modes, index));
    }
    QCOMPARE(sizes, QSet<QSize>({QSize(2560, 1440)}));
}

void WineDisplayModeTest::everyRecordIsOneWineCanRead()
{
    const QByteArray modes = wineDisplayModes(QSize(1920, 1080), 120);
    QVERIFY(wineDisplayModeCount(modes) > 0);
    for (qsizetype index = 0; index < wineDisplayModeCount(modes); ++index) {
        // Wine reads the list in records of this size and asserts that no mode
        // carries driver data of its own.
        QCOMPARE(shortNumber(modes, index, s_sizeAt), s_recordSize);
        QCOMPARE(shortNumber(modes, index, s_driverExtraAt), 0);
        QVERIFY(!sizeOf(modes, index).isEmpty());
        const quint32 depth = number(modes, index, s_depthAt);
        QVERIFY(depth == 8 || depth == 16 || depth == 32);
    }
}

void WineDisplayModeTest::offersSixtyHertzAndTheScreensOwnRate()
{
    QSet<quint32> rates;
    const QByteArray modes = wineDisplayModes(QSize(1920, 1080), 144);
    for (qsizetype index = 0; index < wineDisplayModeCount(modes); ++index) {
        rates.insert(number(modes, index, s_rateAt));
    }
    QCOMPARE(rates, QSet<quint32>({60, 144}));
    // A screen no faster than that adds nothing.
    const QByteArray slow = wineDisplayModes(QSize(1920, 1080), 50);
    QCOMPARE(wineDisplayModeCount(slow), wineDisplayModeCount(modes) / 2);
}

void WineDisplayModeTest::tellsTheCurrentModeItsSizeAndPlace()
{
    const QByteArray mode = wineDisplayMode(QSize(2560, 1440), 0);
    // The value holds the record from its dmFields member on.
    QCOMPARE(mode.size(), s_recordSize - 72);
    QCOMPARE(wineDisplayModeSize(mode), QSize(2560, 1440));
    // DM_POSITION, which the modes in the list do not claim.
    QCOMPARE(qFromLittleEndian<quint32>(mode.constData()) & 0x20u, 0x20u);
    QVERIFY(!wineDisplayModeSize(QByteArray()));
    QVERIFY(!wineDisplayModeSize(QByteArray(mode.size(), '\0')));
}

void WineDisplayModeTest::refusesAnEmptySize()
{
    QVERIFY(wineDisplayModes(QSize(), 60).isEmpty());
    QVERIFY(wineDisplayModes(QSize(0, 1440), 60).isEmpty());
    QVERIFY(wineDisplayMode(QSize(), 60).isEmpty());
}

QTEST_GUILESS_MAIN(WineDisplayModeTest)

#include "wine_display_mode_test.moc"
