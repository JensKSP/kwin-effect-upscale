/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "framestatistics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace KWin
{

void UpscaleFrameStatistics::reset()
{
    m_count = 0;
    m_next = 0;
    m_previous = -1;
}

void UpscaleFrameStatistics::record(double milliseconds)
{
    const double previous = m_previous;
    m_previous = milliseconds;
    // The first presentation only starts the clock: an interval needs two.
    // A timestamp that did not advance is the same presentation reported
    // again, and one that went backwards is not a measurement at all.
    if (previous < 0 || milliseconds <= previous) {
        return;
    }
    m_intervals[m_next] = milliseconds - previous;
    m_next = (m_next + 1) % upscaleFrameWindow;
    m_count = std::min(m_count + 1, upscaleFrameWindow);
}

size_t UpscaleFrameStatistics::frames() const
{
    return m_count;
}

double UpscaleFrameStatistics::averageRate() const
{
    if (m_count == 0) {
        return -1;
    }
    const double total = std::accumulate(m_intervals.begin(), m_intervals.begin() + m_count, 0.0);
    return total > 0 ? 1000.0 * double(m_count) / total : -1;
}

std::array<double, upscaleFrameWindow> UpscaleFrameStatistics::sorted(size_t *count) const
{
    std::array<double, upscaleFrameWindow> values{};
    std::copy(m_intervals.begin(), m_intervals.begin() + m_count, values.begin());
    std::sort(values.begin(), values.begin() + m_count);
    *count = m_count;
    return values;
}

double UpscaleFrameStatistics::lowRate(double fraction) const
{
    size_t count = 0;
    const std::array<double, upscaleFrameWindow> values = sorted(&count);
    // The tail has to hold at least two frames before its mean says anything
    // that the worst single frame did not already say.
    const auto tail = size_t(double(count) * fraction);
    if (count == 0 || tail < 2) {
        return -1;
    }
    // The slowest frames are the largest intervals, at the end of the order.
    const double total = std::accumulate(values.begin() + (count - tail), values.begin() + count, 0.0);
    return total > 0 ? 1000.0 * double(tail) / total : -1;
}

double UpscaleFrameStatistics::percentileFrameTime(double fraction) const
{
    size_t count = 0;
    const std::array<double, upscaleFrameWindow> values = sorted(&count);
    if (count == 0) {
        return -1;
    }
    // Nearest-rank: the smallest interval that at least this share of frames
    // are at or below. No interpolation, so the answer is always a frame time
    // that really occurred.
    const auto rank = size_t(std::ceil(std::clamp(fraction, 0.0, 1.0) * double(count)));
    return values[std::clamp<size_t>(rank, 1, count) - 1];
}

double UpscaleFrameStatistics::worstFrameTime() const
{
    if (m_count == 0) {
        return -1;
    }
    return *std::max_element(m_intervals.begin(), m_intervals.begin() + m_count);
}

} // namespace KWin
