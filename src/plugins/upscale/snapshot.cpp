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
        return i18n("not scaling: %1", describeRefusal(snapshot.refusal));
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
    // "Selected", not "recognized": nothing here identifies a game, and a
    // window that merely fills the screen must not be presented as a match.
    return i18n("Upscale: selected %1", application(snapshot));
}

QString upscaleBasicSummary(const UpscaleSnapshot &snapshot)
{
    return i18n("%1 → %2, %3", sizeText(snapshot.supplied), sizeText(snapshot.destination), processing(snapshot));
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
                snapshot.output.isEmpty() ? unknown() : snapshot.output, measurement(snapshot));
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
    const QString wish = snapshot.preset == ResolutionPreset::Automatic
        ? i18n("Automatic (no request)")
        : i18n("Select %1 × %2 in the game", QString::number(snapshot.desired.width), QString::number(snapshot.desired.height));
    QString state;
    if (snapshot.selected) {
        if (snapshot.scaling) {
            state = i18n("FSR 1, sharpening %1%", qRound(snapshot.sharpening * 100));
        } else if (snapshot.refusal != UpscaleRefusal::None) {
            // A selected window carries the reason its last frame was handed
            // back, which describes that frame rather than the window.
            state = i18n("Eligible buffer; the last frame was not scaled because %1", describeRefusal(snapshot.refusal));
        } else {
            state = i18n("Eligible buffer; waiting for a compatible render pass.");
        }
    } else {
        state = i18n("Inactive: %1", describeRefusal(snapshot.refusal));
        if (snapshot.refusal == UpscaleRefusal::UnsupportedBufferFormat) {
            state += QLatin1Char(' ') + i18n("Supplied format: %1.", snapshot.format);
        }
    }
    return i18n("Desired: %1\nSupplied input: %2\nDestination: %3\n%4\nHDR follows KWin colour management. Actual VRR presentation is not measured.",
                wish, sizeText(snapshot.supplied), sizeText(snapshot.destination), state);
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
    lines.append(i18n("Configuration: %1, desired %2, sharpening %3",
                      snapshot.enabled ? i18n("enabled") : i18n("disabled"), desiredText(snapshot),
                      snapshot.sharpening > 0 ? i18n("RCAS %1%", qRound(snapshot.sharpening * 100)) : i18n("off")));
    lines.append(i18n("Geometry: supplied %1, destination %2, output scale %3",
                      sizeText(snapshot.supplied), sizeText(snapshot.destination),
                      QString::number(snapshot.outputScale, 'f', 2)));
    lines.append(i18n("Processing: buffer format %1, resources %2, scanout blocked by this effect: %3",
                      snapshot.format.isEmpty() ? unknown() : snapshot.format,
                      snapshot.failed ? i18n("failed") : i18n("ready"),
                      snapshot.blocksScanout ? i18n("yes") : i18n("no")));
    // Only the destination colour description is observed here. Nothing in
    // this view establishes that variable refresh is actually in use.
    lines.append(i18n("Colour: destination transfer %1, reference luminance %2 cd/m², VRR not observed",
                      transferName(snapshot.transferFunction),
                      snapshot.referenceLuminance > 0 ? QString::number(snapshot.referenceLuminance, 'f', 0) : unknown()));
    return lines.join(QLatin1Char('\n'));
}

} // namespace KWin
