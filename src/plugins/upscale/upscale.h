/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"
#include "display.h"
#include "eligibility.h"

#include <QPointer>
#include <QString>

#include <memory>

namespace KWin
{

class UpscaleScaler;

/** Scales one eligible fullscreen surface from its supplied buffer size. */
class UpscaleEffect : public Effect
{
    Q_OBJECT
    // Declared before status so that the support information KWin assembles
    // from these properties keeps the single-line identity ahead of the
    // multi-line status, which is what the settings page parses.
    Q_PROPERTY(QString build READ build)
    Q_PROPERTY(QString status READ status)

public:
    UpscaleEffect();
    // The supplied renderer is borrowed until a render-device change replaces
    // it. A null renderer uses KWin's scene renderer.
    explicit UpscaleEffect(ItemRenderer *renderer);
    ~UpscaleEffect() override;

    static bool supported();

    void reconfigure(ReconfigureFlags flags) override;
    UpscalePaintResult paintScreen(const RenderTarget &target, const RenderViewport &viewport, int mask,
                                   const UpscaleRegion &region, UpscaleOutput *screen) override;
#if UPSCALE_RENDER_DEVICE_API
    void prePaintScreen(ScreenPrePaintData &data) override;
#endif
    bool isActive() const override;
    bool blocksDirectScanout() const override;
    int requestedEffectChainPosition() const override;
    UpscalePaintResult drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                  int mask, const UpscaleRegion &region, WindowPaintData &data) override;
    QString build() const;
    QString status() const;

private:
    // The window this effect would scale, or null with the one condition that
    // refused it. The reason is computed only when asked for, because every
    // painted frame asks for the candidate and none of them asks why.
    EffectWindow *candidate(UpscaleRefusal *refusal = nullptr) const;
    // The window the on-screen display describes: the candidate, or the
    // active fullscreen window that was refused, which is the case a
    // developer needs to see explained.
    EffectWindow *displayed() const;
    // The render target is the frame being painted, and null when the caller
    // is outside a paint pass and colour is therefore not observable.
    UpscaleSnapshot snapshot(EffectWindow *window, const RenderTarget *target) const;
    void paintDisplay(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen);
    void watchWindow(EffectWindow *window);

    std::unique_ptr<UpscaleScaler> m_scaler;
    // The candidate whose render target this scaler cannot handle. Held as a
    // window rather than a flag so a different one is always tried again.
    mutable QPointer<EffectWindow> m_unsupportedColors;
    bool m_enabled = true;
    bool m_failed = false;
    double m_strength = 0;
    ItemRenderer *m_renderer = nullptr;
    QPointer<EffectWindow> m_renderedWindow;
    QSize m_renderedInput;
    // Why the last paint pass over the candidate could not be replaced. It
    // describes one frame rather than the window, so it is diagnostic only and
    // never keeps the next frame from being scaled.
    UpscaleRefusal m_passRefusal = UpscaleRefusal::None;
    UpscaleDisplay m_display;
    // The build this effect came from. Empty where the generated record is not
    // part of the build, as in a copy of this folder inside KWin.
    QString m_build;
};

} // namespace KWin
