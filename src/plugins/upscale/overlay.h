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
     * at. Rebuilding is skipped when neither changed, so a repeated snapshot
     * of unchanged state costs nothing.
     */
    void setText(const QString &text, double scale);

    /** The logical size the text occupies, for placement and repaints. */
    QSizeF size() const;

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
    QImage m_image;
    std::unique_ptr<GLTexture> m_texture;
};

} // namespace KWin
