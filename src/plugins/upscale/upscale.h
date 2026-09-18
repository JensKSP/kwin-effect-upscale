/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "compatibility.h"

#include <QPointer>
#include <memory>

namespace KWin
{

class UpscaleScaler;

/** Scales one eligible fullscreen surface from its supplied buffer size. */
class UpscaleEffect : public Effect
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status)

public:
    UpscaleEffect();
    ~UpscaleEffect() override;

    static bool supported();

    void reconfigure(ReconfigureFlags flags) override;
#if UPSCALE_NEW_API
    void prePaintScreen(ScreenPrePaintData &data) override;
#endif
    bool isActive() const override;
    bool blocksDirectScanout() const override;
    int requestedEffectChainPosition() const override;
    UpscalePaintResult drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                  int mask, const UpscaleRegion &region, WindowPaintData &data) override;
    QString status() const;

private:
    EffectWindow *candidate() const;
    static bool eligible(EffectWindow *window);
    void watchWindow(EffectWindow *window);

    std::unique_ptr<UpscaleScaler> m_scaler;
    bool m_enabled = true;
    bool m_failed = false;
    double m_strength = 0;
    ItemRenderer *m_renderer = nullptr;
    QPointer<EffectWindow> m_renderedWindow;
    QSize m_renderedInput;
};

} // namespace KWin
