/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "placement.h"

#include <algorithm>

namespace KWin
{

UpscaleCorner upscaleCorner(int stored)
{
    return UpscaleCorner(std::clamp(stored, 0, upscaleCornerCount - 1));
}

// The order the settings list them in, wrapping at the end. A displaced block
// steps along this order rather than to the nearest corner in pixels, because
// what the person is reading is the list in front of them.
static UpscaleCorner nextCorner(UpscaleCorner corner)
{
    return UpscaleCorner((int(corner) + 1) % upscaleCornerCount);
}

// The first corner after @p from that no entry other than @p moving occupies.
static UpscaleCorner freeCornerAfter(std::span<const UpscaleCorner> corners, std::size_t moving, UpscaleCorner from)
{
    UpscaleCorner candidate = from;
    for (int step = 0; step < upscaleCornerCount; ++step) {
        candidate = nextCorner(candidate);
        bool taken = false;
        for (std::size_t other = 0; other < corners.size(); ++other) {
            taken = taken || (other != moving && corners[other] == candidate);
        }
        if (!taken) {
            return candidate;
        }
    }
    // Every corner is spoken for, which needs at least as many displays as
    // corners. There are three and four. Staying put is the honest answer:
    // the stacking in place() then keeps the two blocks off each other.
    return from;
}

void upscaleTakeCorner(std::span<UpscaleCorner> corners, std::size_t moved, UpscaleCorner corner)
{
    if (moved >= corners.size()) {
        return;
    }
    corners[moved] = corner;
    // At most one other entry can hold it, because the entries were distinct
    // before this call. The loop does not rely on that being true.
    for (std::size_t other = 0; other < corners.size(); ++other) {
        if (other != moved && corners[other] == corner) {
            corners[other] = freeCornerAfter(corners, other, corner);
        }
    }
}

void upscaleSeparateCorners(std::span<UpscaleCorner> corners)
{
    for (std::size_t entry = 0; entry < corners.size(); ++entry) {
        for (std::size_t earlier = 0; earlier < entry; ++earlier) {
            if (corners[entry] == corners[earlier]) {
                corners[entry] = freeCornerAfter(corners, entry, corners[entry]);
                break;
            }
        }
    }
}

UpscaleCornerLayout::UpscaleCornerLayout(const QPointF &origin, const QSizeF &size, double margin)
    : m_origin(origin)
    , m_size(size)
    // A margin is meant to keep text off the edge of a television, not to be
    // the whole of a quarter. Two of them come off each axis of a quarter, so
    // an output smaller than four margins would have nowhere left to draw and
    // would show nothing at all. There it shrinks; on any real output, where
    // the margin is a rounding error beside the screen, it is what was asked
    // for. place() and budget() have to agree, so it is decided once, here.
    , m_margin(std::min(margin, std::max(std::min(size.width(), size.height()) / 8, 0.0)))
{
}

QSizeF UpscaleCornerLayout::budget(UpscaleCorner corner) const
{
    // Half the output each way is the quarter this corner owns. Two margins
    // come off each axis: one at the screen edge, one at the centre line.
    const double width = (m_size.width() / 2) - (2 * m_margin);
    const double height = (m_size.height() / 2) - (2 * m_margin) - m_used[std::size_t(corner)];
    // An output too small to hold a margin would otherwise ask for a negative
    // amount of room, which is not the same thing as asking for none.
    return QSizeF(std::max(width, 0.0), std::max(height, 0.0));
}

QPointF UpscaleCornerLayout::place(UpscaleCorner corner, const QSizeF &size)
{
    const bool atTop = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::TopRight;
    const bool atLeft = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::BottomLeft;
    double &used = m_used[std::size_t(corner)];
    const double x = atLeft ? m_origin.x() + m_margin
                            : m_origin.x() + m_size.width() - m_margin - size.width();
    const double y = atTop ? m_origin.y() + m_margin + used
                           : m_origin.y() + m_size.height() - m_margin - used - size.height();
    // The gap between two blocks in one corner is the margin again, so that
    // they read as two plates rather than as one split down the middle.
    used += size.height() + m_margin;
    // A block wider or taller than the output it is drawn on would otherwise
    // start off the left or top edge, where its beginning cannot be read at
    // all. Keep the start on the screen and let the far end be what is lost.
    // A block that was fitted to budget() cannot reach here; one drawn on an
    // output with no room for a margin at all still can.
    return QPointF(std::max(x, m_origin.x()), std::max(y, m_origin.y()));
}

} // namespace KWin
