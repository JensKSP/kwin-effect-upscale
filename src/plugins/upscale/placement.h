/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QPointF>
#include <QSizeF>

#include <array>
#include <cstddef>
#include <span>

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
 * Moves @p moved to @p corner, pushing whatever held it out of the way.
 *
 * The displaced entry takes the next free corner counting forward from the
 * contested one, wrapping, rather than the corner @p moved just left. That is
 * deliberately not a swap: a person dragging one display around the screen is
 * choosing where that display goes, and having a second one jump into the
 * place they just came from reads as the screen arguing back.
 *
 * There is always somewhere to go while there are fewer displays than corners.
 */
void upscaleTakeCorner(std::span<UpscaleCorner> corners, std::size_t moved, UpscaleCorner corner);

/**
 * Separates corners that name the same place, keeping the earlier entry.
 *
 * Nothing in the settings can produce a duplicate, because every change goes
 * through upscaleTakeCorner(). A hand-edited configuration file can, and two
 * blocks drawn over one another is exactly what this placement exists to
 * prevent, so the effect separates them again when it reads them.
 */
void upscaleSeparateCorners(std::span<UpscaleCorner> corners);

/**
 * Places the display's blocks on one output, one corner at a time.
 *
 * The blocks are separate things a person reads for different reasons: a
 * message that goes away, measurements that stay, and a developer dump. They
 * are therefore drawn in their own corners rather than as one growing column,
 * and only the corner a block was given decides where it lands.
 *
 * Each corner owns one quarter of the output and a block is confined to it.
 * That is what makes overlapping impossible rather than merely unlikely: a
 * block bounded by its own quarter cannot reach into anyone else's, whatever
 * the output's scale factor does to the ratio between the text and the screen.
 * The alternative, moving blocks apart once they collide, depends on the order
 * they are placed in and has to be reconsidered every time a block is added.
 *
 * Two blocks can still be sent to the same corner by a configuration file that
 * was edited by hand. They stack away from that corner in the order they are
 * placed, sharing the one quarter, so the first one keeps the position it had
 * when it was alone and the next one appears beside it instead of on top of it.
 */
class UpscaleCornerLayout
{
public:
    /** @p origin and @p size are the output's logical geometry. */
    UpscaleCornerLayout(const QPointF &origin, const QSizeF &size, double margin);

    /**
     * The room left in this corner: its quarter of the output, inset by the
     * margin on all four sides, less whatever has already been placed there.
     *
     * The inset applies to the two centre lines as well as to the two screen
     * edges, so blocks in neighbouring quarters stand twice the margin apart
     * and read as separate plates rather than as one band across the screen.
     */
    QSizeF budget(UpscaleCorner corner) const;

    /** The top left corner for a block of this size, and reserves its space. */
    QPointF place(UpscaleCorner corner, const QSizeF &size);

private:
    QPointF m_origin;
    QSizeF m_size;
    double m_margin;
    std::array<double, upscaleCornerCount> m_used{};
};

} // namespace KWin
