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

UpscaleCornerLayout::UpscaleCornerLayout(const QPointF &origin, const QSizeF &size, double margin)
    : m_origin(origin)
    , m_size(size)
    , m_margin(margin)
{
}

QPointF UpscaleCornerLayout::place(UpscaleCorner corner, const QSizeF &size)
{
    const bool atTop = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::TopRight;
    const bool atLeft = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::BottomLeft;
    double &used = m_used[size_t(corner)];
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
    return QPointF(std::max(x, m_origin.x()), std::max(y, m_origin.y()));
}

} // namespace KWin
