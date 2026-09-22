/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <QImage>
#include <QSizeF>
#include <QString>

#include <memory>

namespace KWin
{

class GLTexture;

/**
 * The passive on-screen display: a block of text drawn over the output.
 *
 * It is deliberately not a window and not a Qt scene. It takes no input, holds
 * no focus and is drawn after the game pass, so it never reaches the captured
 * texture the scaler reads and never appears enlarged. Its only state is the
 * text it was last given, and the image and texture built from it.
 */
class UpscaleOverlay
{
public:
    UpscaleOverlay();
    ~UpscaleOverlay();

    /**
     * Replaces the displayed text, at the destination scale it will be drawn
     * at. Rebuilding is skipped when nothing changed, so a repeated snapshot
     * of unchanged state costs nothing.
     *
     * @p emphasis multiplies the session's font size for this block alone. A
     * heads-up display read at a glance during play needs to be larger than a
     * diagnostic dump read by leaning towards the screen, and both are drawn
     * by this class.
     *
     * Nothing is laid out here. The room this block is allowed is known only
     * to whoever is painting an output, and laying the text out twice for one
     * snapshot would cost more than the throttling that produced it saves.
     * It happens on the first question that needs an answer instead, so a
     * caller with no output to place against still gets a sized block.
     */
    void setText(const QString &text, double scale, double emphasis = 1);

    /**
     * Lays the text out to fit within @p budget logical pixels.
     *
     * The block is drawn at the size it asked for when that fits. When it does
     * not, it is laid out again at the largest fraction of that size which
     * does, down to the smallest readable font; whatever still falls outside
     * is cropped. The budget is therefore a bound and not a preference, which
     * is what lets the caller promise that two blocks never meet.
     *
     * A default-constructed budget means no bound, for a caller that has no
     * output to measure against. A budget of no size means no room, which is
     * a different answer and leaves nothing drawn.
     */
    void fit(const QSizeF &budget);

    /** The logical size the text occupies, for placement and repaints. */
    QSizeF size() const;

private:
    /** Lays the text out for the current budget, if that has not happened. */
    void layOut() const;

public:
    /** The text as it was last laid out. */
    QString text() const;

    bool isEmpty() const;

    /** Drops the text and every resource built from it. */
    void release();

    /**
     * Draws the text with its top left corner at this logical position. The
     * caller owns placement, because only it knows which output is being
     * painted and where the game sits on it.
     */
    bool paint(const RenderTarget &target, const RenderViewport &viewport, const QPointF &position);

private:
    QString m_text;
    double m_scale = 1;
    double m_emphasis = 1;
    // The budget the current image was laid out for, so that a repeated frame
    // on an unchanged output does not lay the same text out again.
    QSizeF m_budget;
    // Laying out is deferred to the first question that needs the answer, so
    // that a block whose budget arrives after its text is built only once.
    mutable bool m_fitted = false;
    mutable QImage m_image;
    std::unique_ptr<GLTexture> m_texture;
};

} // namespace KWin
