/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "compatibility.h"
#include "display.h"
#include "eligibility.h"
#include "settings.h"

#include <QHash>
#include <QPointer>
#include <QString>

#include <memory>

namespace KWin
{

class UpscaleModeOverride;
class UpscalePreparation;
class UpscaleWaylandScale;
class UpscaleX11Resolution;
class UpscaleWaylandScale;
class UpscaleScaler;
class Window;

/** Scales one eligible surface per output from its supplied buffer size. */
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
    void grabbedKeyboardEvent(QKeyEvent *event) override;
    /** Whether the X11 resolution control has nothing in flight; see UpscaleX11Resolution::settled(). */
    bool x11RequestsSettled() const;
    int requestedEffectChainPosition() const override;
    UpscalePaintResult drawWindow(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                                  int mask, const UpscaleRegion &region, WindowPaintData &data) override;
    QString build() const;
    QString status() const;
    /**
     * The effect's own screen pass: what it draws after the chain has painted
     * the frame. Separate from paintScreen so that it can be driven against a
     * different target than the one the chain painted into.
     */
    void paintDisplay(const RenderTarget &target, const RenderViewport &viewport, UpscaleOutput *screen);

private:
    // The window this effect would scale, or null with the one condition that
    // refused it. Paint passes share this decision with their diagnostics.
    EffectWindow *candidate(UpscaleRefusal *refusal = nullptr, UpscaleOutput *output = nullptr) const;
    EffectWindow *findCandidate(UpscaleRefusal *refusal, UpscaleOutput *output) const;
    // The window the on-screen display describes: the candidate, or the
    // active fullscreen window that was refused, which is the case a
    // developer needs to see explained.
    EffectWindow *displayed() const;
    // The render target is the frame being painted, and null when the caller
    // is outside a paint pass and colour is therefore not observable.
    UpscaleSnapshot snapshot(EffectWindow *window, const RenderTarget *target) const;
    void describeApplication(UpscaleSnapshot &state, const Window *window, UpscalePresentation presentation) const;
    void watchWindow(EffectWindow *window);
    void watchOutput(UpscaleOutput *output);
    UpscaleRefusal rememberPassRefusal(EffectWindow *window, UpscaleRefusal refusal);
    void releaseWhatTheGameLeftBehind();

    // Reuse selection only within one synchronous screen paint. Outside it,
    // queries must see current buffer, geometry, focus and lock state.
    bool m_inPaint = false;
    UpscaleOutput *m_paintOutput = nullptr;
    mutable UpscaleOutput *m_candidateOutput = nullptr;
    mutable bool m_candidateCached = false;
    mutable QPointer<EffectWindow> m_candidate;
    mutable UpscaleRefusal m_candidateRefusal = UpscaleRefusal::NoWindow;
    std::unique_ptr<UpscaleScaler> m_scaler;
    // Talks to a recognized application when it connects, which is before any
    // window of it exists. It therefore outlives individual windows and is
    // created once, not per candidate.
    std::unique_ptr<UpscaleModeOverride> m_modeOverride;
    std::unique_ptr<UpscaleX11Resolution> m_x11Resolution;
    std::unique_ptr<UpscaleWaylandScale> m_waylandScale;
    // Asks a helper, and the user, about a program that cannot be made to
    // render smaller while it runs. Declared after the X11 control it uses,
    // so that it goes first.
    std::unique_ptr<UpscalePreparation> m_preparation;

    /** Auto's Wayland half for the selected window, or giving its scale back. */
    void askForSmallerBuffer(UpscaleOutput *output, EffectWindow *candidate, const UpscaleApplication *claimed) const;
    bool autoWaiting() const;
    // Refused windows are independent. Output colour/configuration changes
    // and window output changes invalidate their refusal without reconfiguration.
    QList<QPointer<EffectWindow>> m_unsupportedColors;
    // Resolved for the window that was selected, because every preference is
    // now a global value a profile may override and no two windows need agree.
    // Updated where the candidate is, which is off the paint path.
    mutable UpscaleSettings m_settings;
    bool m_failed = false;
    // The largest texture this GPU will allocate, read from the driver rather
    // than assumed. The scaler needs one at the destination size.
    int m_maximumTexture = 0;
    ItemRenderer *m_renderer = nullptr;
    QHash<EffectWindow *, QSize> m_renderedInputs;
    // Why the last paint pass over the candidate could not be replaced. It
    // describes one frame rather than the window, so it is diagnostic only and
    // never keeps the next frame from being scaled.
    QHash<EffectWindow *, UpscaleRefusal> m_passRefusals;
    UpscaleDisplay m_display;
    // The build this effect came from. Empty where the generated record is not
    // part of the build, as in a copy of this folder inside KWin.
    QString m_build;
};

} // namespace KWin
