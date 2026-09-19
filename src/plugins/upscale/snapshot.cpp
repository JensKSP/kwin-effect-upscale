/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

#include "config-kwin.h"

#include <KLocalizedString>

namespace KWin
{

static QString unknown()
{
    return i18nc("A value the effect cannot observe", "unknown");
}

static QString sizeText(const QSize &size)
{
    // Pixel counts are substituted as text on purpose. Passing the integers
    // lets the locale group them, and "3.840 × 2.160" reads as two fractional
    // numbers rather than as a resolution.
    return size.isEmpty() ? unknown()
                          : i18n("%1 × %2", QString::number(size.width()), QString::number(size.height()));
}

static QString presetName(ResolutionPreset preset)
{
    switch (preset) {
    case ResolutionPreset::Automatic:
        return i18n("Automatic");
    case ResolutionPreset::Native:
        return i18n("Native");
    case ResolutionPreset::UltraQuality:
        return i18n("Ultra Quality");
    case ResolutionPreset::Quality:
        return i18n("Quality");
    case ResolutionPreset::Balanced:
        return i18n("Balanced");
    case ResolutionPreset::Performance:
        return i18n("Performance");
    case ResolutionPreset::Custom:
        return i18n("Custom");
    }
    return unknown();
}

static QString transferName(int transferFunction)
{
    switch (transferFunction) {
    case int(TransferFunction::sRGB):
        return i18n("sRGB");
    case int(TransferFunction::linear):
        return i18n("linear");
    case int(TransferFunction::PerceptualQuantizer):
        return i18n("PQ");
    case int(TransferFunction::gamma22):
        return i18n("gamma 2.2");
    default:
        return unknown();
    }
}

static QString refusalText(const UpscaleSnapshot &snapshot)
{
    QString reason = describeRefusal(snapshot.refusal);
    if (snapshot.refusal == UpscaleRefusal::UnsupportedBufferFormat) {
        reason += QLatin1Char(' ') + i18n("Supplied format: %1.", snapshot.format.isEmpty() ? unknown() : snapshot.format);
    }
    return reason;
}

// What the effect is doing with the buffer, in the words the settings page
// uses. A bypassed or refused path is never described as the configured
// scaler being active.
static QString processing(const UpscaleSnapshot &snapshot)
{
    if (snapshot.scaling) {
        return snapshot.sharpening > 0
            ? i18n("FSR 1 with RCAS sharpening %1%", qRound(snapshot.sharpening * 100))
            : i18n("FSR 1, no sharpening");
    }
    if (snapshot.refusal != UpscaleRefusal::None) {
        return i18n("not scaling: %1", refusalText(snapshot));
    }
    // Eligible, but no frame has come through the scaler yet.
    return i18n("not scaling yet");
}

static QString application(const UpscaleSnapshot &snapshot)
{
    if (!snapshot.window.isEmpty()) {
        return snapshot.window;
    }
    return snapshot.application.isEmpty() ? unknown() : snapshot.application;
}

QString upscaleAnnouncement(const UpscaleSnapshot &snapshot)
{
    // "Selected" for a window the effect merely picked, "recognized" only
    // where its identity matched the catalogue. A window that fills the
    // screen must not be presented as a match on that ground alone.
    if (snapshot.recognized.isEmpty()) {
        return i18n("Upscale: selected %1", application(snapshot));
    }
    return i18n("Upscale: recognized %1", snapshot.recognized);
}

QString upscaleBasicSummary(const UpscaleSnapshot &snapshot)
{
    return i18n("%1 → %2, %3", sizeText(snapshot.supplied), sizeText(snapshot.destination), processing(snapshot));
}

static QString presentationName(int mode)
{
    switch (static_cast<PresentationMode>(mode)) {
    case PresentationMode::VSync:
        return i18n("fixed refresh");
    case PresentationMode::AdaptiveSync:
        return i18n("adaptive sync");
    case PresentationMode::Async:
        return i18n("tearing");
    case PresentationMode::AdaptiveAsync:
        return i18n("adaptive sync with tearing");
    }
    return unknown();
}

// Frames as the screen showed them. An average alone hides the stutter that
// decides whether something feels smooth, so the slow tail is reported beside
// it, in the two forms that are both called a "one per cent low" and do not
// mean the same thing: the mean of the slowest hundredth, and the frame time
// that all but the slowest hundredth beat.
static QString presented(const UpscaleSnapshot &snapshot)
{
    if (snapshot.presentedRate < 0) {
        return i18n("Presented: %1", unknown());
    }
    QString text = i18n("Presented: %1/s average", QString::number(snapshot.presentedRate, 'f', 1));
    if (snapshot.presentedLow > 0) {
        // A literal percent sign, written once: KLocalizedString substitutes
        // numbered placeholders and does not collapse a doubled one the way
        // printf does, so "%%" would reach the screen as it stands here.
        text += i18n(", 1% low %1/s", QString::number(snapshot.presentedLow, 'f', 1));
    }
    if (snapshot.presentedPercentile > 0) {
        text += i18n(", 99th percentile %1 ms", QString::number(snapshot.presentedPercentile, 'f', 1));
    }
    if (snapshot.presentedWorst > 0) {
        text += i18n(", worst %1 ms", QString::number(snapshot.presentedWorst, 'f', 1));
    }
    return text + i18n(" (%1 frames, %2)", QString::number(snapshot.presentedFrames), presentationName(snapshot.presentation));
}

static QString measurement(const UpscaleSnapshot &snapshot)
{
    if (snapshot.clientUpdates < 0) {
        return i18n("Client buffer updates: %1", unknown());
    }
    // Name what is counted. A compositor repaint is not the game's frame rate,
    // so the two are reported separately and never merged into one "FPS".
    return i18n("Client buffer updates: %1/s, compositor repaints: %2/s (%3 s sample, %4 s ago)",
                QString::number(snapshot.clientUpdates, 'f', 1), QString::number(snapshot.repaints, 'f', 1),
                QString::number(snapshot.interval, 'f', 1), QString::number(snapshot.sampleAge, 'f', 1));
}

QString upscaleStatistics(const UpscaleSnapshot &snapshot)
{
    return i18n("Upscale: %1\n%2 → %3 on %4\n%5",
                processing(snapshot), sizeText(snapshot.supplied), sizeText(snapshot.destination),
                snapshot.output.isEmpty() ? unknown() : snapshot.output, presented(snapshot));
}

static QString desiredText(const UpscaleSnapshot &snapshot)
{
    if (snapshot.preset == ResolutionPreset::Automatic) {
        return i18n("%1 (use the supplied buffer)", presetName(snapshot.preset));
    }
    // A wish, not an applied setting: nothing asks the game for this size yet.
    return i18n("%1, %2% of the destination (%3)", presetName(snapshot.preset),
                qRound(resolutionRatio(snapshot.preset, snapshot.percentage) * 100),
                sizeText(QSize(snapshot.desired.width, snapshot.desired.height)));
}

static QString selection(const UpscaleSnapshot &snapshot)
{
    QStringList states;
    states.append(snapshot.activeWindow ? i18n("active") : i18n("not active"));
    states.append(snapshot.fullScreen ? i18n("fullscreen") : i18n("not fullscreen"));
    states.append(snapshot.selected ? i18n("selected") : i18n("not selected"));
    return states.join(QStringLiteral(", "));
}

QString upscaleStatusText(const UpscaleSnapshot &snapshot)
{
    QString wish;
    if (snapshot.advertised.isValid()) {
        // Advertised, not applied. The committed input below is the only
        // evidence of what the application actually did with it.
        wish = i18n("%1 requested from %2 as its screen mode",
                    sizeText(snapshot.advertised),
                    snapshot.recognized.isEmpty() ? application(snapshot) : snapshot.recognized);
    } else if (snapshot.preset == ResolutionPreset::Automatic) {
        wish = i18n("Automatic (no request)");
    } else {
        wish = i18n("Select %1 × %2 in the game",
                    QString::number(snapshot.desired.width), QString::number(snapshot.desired.height));
    }
    QString state;
    if (snapshot.selected) {
        if (snapshot.scaling) {
            state = i18n("FSR 1, sharpening %1%", qRound(snapshot.sharpening * 100));
        } else if (snapshot.refusal != UpscaleRefusal::None) {
            // A selected window carries the reason its last frame was handed
            // back, which describes that frame rather than the window.
            state = i18n("Eligible buffer; the last frame was not scaled because %1", refusalText(snapshot));
        } else {
            state = i18n("Eligible buffer; waiting for a compatible render pass.");
        }
    } else {
        state = i18n("Inactive: %1", refusalText(snapshot));
    }
    // The rate decides this, not the mode. Changing the displayed window
    // restarts the frame sampling without clearing the last mode the screen
    // presented in, so a mode can outlive the measurement it belonged to and
    // this would otherwise report a rate of minus one per second.
    const QString presentation = snapshot.presentedRate < 0
        ? i18n("Presentation is not being measured; the on-screen display measures it while it is shown.")
        : i18n("Presented at %1/s, %2.", QString::number(snapshot.presentedRate, 'f', 1),
               presentationName(snapshot.presentation));
    return i18n("Desired: %1\nSupplied input: %2\nDestination: %3\n%4\n%5\nHDR follows KWin colour management.",
                wish, sizeText(snapshot.supplied), sizeText(snapshot.destination), state, presentation);
}

QString upscaleDeveloperInformation(const UpscaleSnapshot &snapshot)
{
    QStringList lines;
    lines.append(i18n("Build: %1", snapshot.build.isEmpty() ? unknown() : snapshot.build));
    // The KWin version is the one this plugin was compiled against. The
    // running compositor may be a different one, and this does not observe it.
    lines.append(i18n("Runtime: %1 build, %2, built against KWin %3, Qt %4", snapshot.buildType, snapshot.graphics,
                      QString(KWIN_VERSION_STRING), QString::fromLatin1(qVersion())));
    lines.append(i18n("Window: %1 (%2) on %3", application(snapshot),
                      snapshot.application.isEmpty() ? unknown() : snapshot.application,
                      snapshot.output.isEmpty() ? unknown() : snapshot.output));
    lines.append(i18n("Selection: %1, %2", selection(snapshot), processing(snapshot)));
    lines.append(i18n("Application: %1, method %2, advertised %3",
                      snapshot.recognized.isEmpty() ? i18n("not recognized") : snapshot.recognized,
                      describeControlMethod(snapshot.method),
                      snapshot.advertised.isValid() ? sizeText(snapshot.advertised) : i18n("nothing")));
    lines.append(i18n("Configuration: %1, desired %2, sharpening %3",
                      snapshot.enabled ? i18n("enabled") : i18n("disabled"), desiredText(snapshot),
                      snapshot.sharpening > 0 ? i18n("RCAS %1%", qRound(snapshot.sharpening * 100)) : i18n("off")));
    lines.append(i18n("Geometry: supplied %1, destination %2, output scale %3",
                      sizeText(snapshot.supplied), sizeText(snapshot.destination),
                      QString::number(snapshot.outputScale, 'f', 2)));
    lines.append(measurement(snapshot));
    // The frames the screen actually showed, and the mode it showed them in.
    // Whether variable refresh was in use is read from that mode, so this view
    // no longer has to say the question is unanswered.
    lines.append(presented(snapshot));
    lines.append(i18n("Frame: render target orientation %1, largest texture this GPU allows %2",
                      snapshot.targetTransform < 0 ? unknown() : QString::number(snapshot.targetTransform),
                      snapshot.maximumTexture > 0 ? QString::number(snapshot.maximumTexture) : unknown()));
    lines.append(i18n("Processing: buffer format %1, resources %2, scanout blocked by this effect: %3",
                      snapshot.format.isEmpty() ? unknown() : snapshot.format,
                      snapshot.failed ? i18n("failed") : i18n("ready"),
                      snapshot.blocksScanout ? i18n("yes") : i18n("no")));
    // Only the destination colour description is observed here.
    lines.append(i18n("Colour: destination transfer %1, reference luminance %2 cd/m²",
                      transferName(snapshot.transferFunction),
                      snapshot.referenceLuminance > 0 ? QString::number(snapshot.referenceLuminance, 'f', 0) : unknown()));
    return lines.join(QLatin1Char('\n'));
}

} // namespace KWin
