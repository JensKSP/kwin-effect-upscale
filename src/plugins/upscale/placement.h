/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPointF>
#include <QSizeF>

#include <array>

namespace KWin
{

/**
 * Where a block of the on-screen display sits on the output it is drawn on.
 *
 * The order is the one the settings offer and is stored in the configuration,
 * so entries are appended rather than reordered.
 */
enum class UpscaleCorner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

/** How many corners a setting can name. */
inline constexpr int upscaleCornerCount = 4;

/** Reads a stored or otherwise unvalidated corner, clamped to a real one. */
UpscaleCorner upscaleCorner(int stored);

/**
 * Places the display's blocks on one output, one corner at a time.
 *
 * The blocks are separate things a person reads for different reasons: a
 * message that goes away, measurements that stay, and a developer dump. They
 * are therefore drawn in their own corners rather than as one growing column,
 * and only the corner a block was given decides where it lands.
 *
 * Two blocks can still be sent to the same corner, because the corner of the
 * persistent view is the user's choice. They stack away from that corner in
 * the order they are placed, so the first one keeps the position it had when
 * it was alone and the next one appears beside it instead of on top of it.
 */
class UpscaleCornerLayout
{
public:
    /** @p origin and @p size are the output's logical geometry. */
    UpscaleCornerLayout(const QPointF &origin, const QSizeF &size, double margin);

    /** The top left corner for a block of this size, and reserves its space. */
    QPointF place(UpscaleCorner corner, const QSizeF &size);

private:
    QPointF m_origin;
    QSizeF m_size;
    double m_margin;
    std::array<double, upscaleCornerCount> m_used{};
};

} // namespace KWin
