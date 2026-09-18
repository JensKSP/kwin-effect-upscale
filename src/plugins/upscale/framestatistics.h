/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <array>
#include <cstddef>

namespace KWin
{

/** Frames kept. About seventeen seconds at 60 Hz, eight at 120 Hz. */
inline constexpr size_t upscaleFrameWindow = 1024;

/**
 * What the frames actually looked like, in the terms people compare hardware in.
 *
 * An average frame rate hides the stutter that decides whether something feels
 * smooth, which is why a review never reports one alone. The measures below are
 * the ones in common use, computed from the intervals between frames that were
 * really presented on screen rather than from anything this effect submitted:
 * a frame the compositor dropped or repeated is not a frame the user saw.
 *
 * Two different measures are both called the "one per cent low" in the wild,
 * and they do not agree. This class offers both and names them apart, because
 * quoting one as the other is a real way to compare two machines wrongly.
 */
class UpscaleFrameStatistics
{
public:
    /** Add one presentation, given the time it happened. Out-of-order or
     *  duplicate timestamps are ignored rather than recorded as a frame. */
    void record(double milliseconds);
    void reset();

    /** How many intervals are held. One fewer than the presentations seen. */
    size_t frames() const;

    /**
     * Frames per second over the whole window: the count divided by the time
     * it took, which is the reciprocal of the mean interval.
     *
     * Not the mean of each frame's instantaneous rate. Averaging rates weights
     * the fast frames more heavily and reads higher than the machine achieved.
     */
    double averageRate() const;

    /**
     * The mean of the slowest @p fraction of frames, as a rate.
     *
     * This is the "one per cent low" of a hardware review at fraction 0.01:
     * take the slowest one per cent of the frames and average them. It answers
     * "when it was bad, how bad was it on average".
     *
     * Negative when too few frames have been seen for the fraction to describe
     * more than a single frame, because one frame is an anecdote.
     */
    double lowRate(double fraction) const;

    /**
     * The frame time below which @p fraction of frames fall, in milliseconds.
     *
     * At fraction 0.99 this is the 99th percentile frame time, the single
     * number most often quoted for smoothness. Expressed as a rate it is the
     * other thing called a "one per cent low": one sample point rather than an
     * average of a tail, so it reacts to a single bad frame where lowRate()
     * does not.
     */
    double percentileFrameTime(double fraction) const;

    /** The longest interval seen in the window, in milliseconds. */
    double worstFrameTime() const;

private:
    std::array<double, upscaleFrameWindow> sorted(size_t *count) const;

    std::array<double, upscaleFrameWindow> m_intervals{};
    size_t m_count = 0;
    size_t m_next = 0;
    double m_previous = -1;
};

} // namespace KWin
