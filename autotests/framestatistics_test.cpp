/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "framestatistics.h"

#include <QTest>

using namespace KWin;

class FrameStatisticsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reportsNothingWithoutFrames();
    void averagesOverTimeNotOverRates();
    void separatesTheTwoKindsOfLow();
    void needsEnoughFramesForATail();
    void keepsOnlyTheRecentWindow();
    void ignoresRepeatedAndBackwardTimestamps();
};

void FrameStatisticsTest::reportsNothingWithoutFrames()
{
    UpscaleFrameStatistics statistics;
    QCOMPARE(statistics.frames(), size_t(0));
    QVERIFY(statistics.averageRate() < 0);
    QVERIFY(statistics.lowRate(0.01) < 0);
    QVERIFY(statistics.worstFrameTime() < 0);
    // One presentation is not yet an interval.
    statistics.record(0);
    QCOMPARE(statistics.frames(), size_t(0));
    statistics.record(10);
    QCOMPARE(statistics.frames(), size_t(1));
}

// The rate over a window is the count divided by the time, which is not the
// mean of each frame's own rate: averaging rates weights the fast frames more
// and reports a machine as faster than it was.
void FrameStatisticsTest::averagesOverTimeNotOverRates()
{
    UpscaleFrameStatistics statistics;
    double now = 0;
    // One slow frame of 100 ms and one fast frame of 10 ms.
    for (const double interval : {100.0, 10.0}) {
        statistics.record(now);
        now += interval;
    }
    statistics.record(now);
    QCOMPARE(statistics.frames(), size_t(2));
    // Two frames in 110 ms is 18.18/s. The mean of 10/s and 100/s would be 55.
    QVERIFY(std::abs(statistics.averageRate() - 2000.0 / 110.0) < 0.001);
}

// Both of these are called the "one per cent low" by somebody. They answer
// different questions and must never be quoted for one another.
void FrameStatisticsTest::separatesTheTwoKindsOfLow()
{
    UpscaleFrameStatistics statistics;
    double now = 0;
    // 200 frames: 196 at 10 ms, then 4 slow ones, the worst by far.
    for (int frame = 0; frame < 196; ++frame) {
        statistics.record(now);
        now += 10;
    }
    for (const double interval : {40.0, 50.0, 60.0, 200.0}) {
        statistics.record(now);
        now += interval;
    }
    statistics.record(now);
    QCOMPARE(statistics.frames(), size_t(200));

    // The slowest two frames average (60 + 200) / 2 = 130 ms, so 7.7/s.
    QVERIFY(std::abs(statistics.lowRate(0.01) - 2000.0 / 260.0) < 0.01);
    // The 99th percentile frame time is a single frame that really occurred.
    // Of two hundred frames, ninety-nine per cent is a hundred and ninety
    // eight of them, so it is the third slowest, and the two slowest are the
    // tail the measure above averaged: 50 ms against 130 ms, from the same
    // frames. That gap is why the two must not be quoted for one another.
    QCOMPARE(statistics.percentileFrameTime(0.99), 50.0);
    QCOMPARE(statistics.worstFrameTime(), 200.0);
    // The median is unaffected by either tail.
    QCOMPARE(statistics.percentileFrameTime(0.5), 10.0);
}

void FrameStatisticsTest::needsEnoughFramesForATail()
{
    UpscaleFrameStatistics statistics;
    double now = 0;
    for (int frame = 0; frame < 100; ++frame) {
        statistics.record(now);
        now += 10;
    }
    // One per cent of a hundred frames is one frame, which is an anecdote
    // rather than an average, so it is refused.
    QVERIFY(statistics.lowRate(0.01) < 0);
    // A tenth is ten frames and answers.
    QVERIFY(statistics.lowRate(0.1) > 0);
}

void FrameStatisticsTest::keepsOnlyTheRecentWindow()
{
    UpscaleFrameStatistics statistics;
    double now = 0;
    // Fill past Capacity with a slow rate, then run fast for a full window.
    for (size_t frame = 0; frame < upscaleFrameWindow; ++frame) {
        statistics.record(now);
        now += 100;
    }
    // One presentation more than the window, so that the interval bridging
    // the slow past and the fast present is pushed out of it as well.
    for (size_t frame = 0; frame <= upscaleFrameWindow; ++frame) {
        statistics.record(now);
        now += 10;
    }
    QCOMPARE(statistics.frames(), upscaleFrameWindow);
    // Nothing of the slow past survives, so the window really is a window.
    QVERIFY(std::abs(statistics.averageRate() - 100.0) < 0.001);
    QCOMPARE(statistics.worstFrameTime(), 10.0);
}

void FrameStatisticsTest::ignoresRepeatedAndBackwardTimestamps()
{
    UpscaleFrameStatistics statistics;
    statistics.record(0);
    statistics.record(10);
    statistics.record(10); // the same presentation reported twice
    statistics.record(5); // a clock that went backwards
    QCOMPARE(statistics.frames(), size_t(1));
    QCOMPARE(statistics.worstFrameTime(), 10.0);
    // A rejected timestamp must not become the baseline. Measuring the next
    // real presentation from it would report an interval nothing took: from
    // the backward 5 this frame would read as 15 ms rather than the 10 it was.
    statistics.record(20);
    QCOMPARE(statistics.frames(), size_t(2));
    QCOMPARE(statistics.worstFrameTime(), 10.0);
}

QTEST_GUILESS_MAIN(FrameStatisticsTest)

#include "framestatistics_test.moc"
