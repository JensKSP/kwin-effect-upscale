/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"
#include "snapshot_text.h"

#include "warningtext.h"

#include "config-kwin.h"

#include <KLocalizedString>

#include <QLocale>

#include <algorithm>

namespace KWin
{

static QString presetName(ResolutionPreset preset)
{
    switch (preset) {
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
    return upscaleUnknownText();
}

static QString transferName(int transferFunction)
{
    switch (transferFunction) {
    case int(TransferFunction::sRGB):
        return i18nc("A transfer function, completing “destination transfer %1”", "sRGB");
    case int(TransferFunction::linear):
        return i18nc("A transfer function, completing “destination transfer %1”", "linear");
    case int(TransferFunction::PerceptualQuantizer):
        return i18nc("A transfer function, completing “destination transfer %1”", "PQ");
    case int(TransferFunction::gamma22):
        return i18nc("A transfer function, completing “destination transfer %1”", "gamma 2.2");
    default:
        return upscaleUnknownText();
    }
}

static QString refusalText(const UpscaleSnapshot &snapshot)
{
    QString reason = describeRefusal(snapshot.refusal);
    if (snapshot.refusal == UpscaleRefusal::UnsupportedBufferFormat) {
        reason += QLatin1Char(' ') + i18n("Supplied format: %1.", snapshot.format.isEmpty() ? upscaleUnknownText() : snapshot.format);
    }
    return reason;
}

// What the effect is doing with the buffer, in the words the settings page
// uses. A bypassed or refused path is never described as the configured
// scaler being active.
static QString processing(const UpscaleSnapshot &snapshot)
{
    if (snapshot.scaling && snapshot.filter == UpscaleFilter::Nearest) {
        return i18nc("What the effect does with the buffer, after the sizes or a window's states", "nearest neighbor");
    }
    if (snapshot.scaling) {
        return snapshot.sharpening > 0
            ? i18nc("What the effect does with the buffer, after the sizes or a window's states", "FSR 1 with RCAS sharpening %1%", qRound(snapshot.sharpening * 100))
            : i18nc("What the effect does with the buffer, after the sizes or a window's states", "FSR 1, no sharpening");
    }
    if (snapshot.refusal != UpscaleRefusal::None) {
        return i18nc("%1 is a reason, written as a clause", "not scaling: %1", refusalText(snapshot));
    }
    // Eligible, but no frame has come through the scaler yet.
    return i18nc("What the effect does with the buffer, after the sizes or a window's states", "not scaling yet");
}

static QString application(const UpscaleSnapshot &snapshot)
{
    if (!snapshot.window.isEmpty()) {
        return snapshot.window;
    }
    return snapshot.application.isEmpty() ? upscaleUnknownText() : snapshot.application;
}

QString upscaleAnnouncement(const UpscaleSnapshot &snapshot)
{
    // "Detected" for a window the effect merely picked, "recognized" only
    // where its identity matched the catalogue. A window that fills the
    // screen must not be presented as a match on that ground alone.
    if (snapshot.recognized.isEmpty()) {
        return i18n("Upscale: detected %1", application(snapshot));
    }
    return i18n("Upscale: recognized %1", snapshot.recognized);
}

QString upscaleBasicSummary(const UpscaleSnapshot &snapshot)
{
    return i18n("%1 → %2, %3", upscaleSuppliedForDisplay(snapshot, upscaleSizeText(snapshot.supplied)), upscaleSizeText(snapshot.destination),
                processing(snapshot));
}

static QString presentationName(int mode)
{
    switch (static_cast<PresentationMode>(mode)) {
    case PresentationMode::VSync:
        return i18nc("How the screen presents, completing “Presented at %1/s, %2.” or a frame count", "fixed refresh");
    case PresentationMode::AdaptiveSync:
        return i18nc("How the screen presents, completing “Presented at %1/s, %2.” or a frame count", "adaptive sync");
    case PresentationMode::Async:
        return i18nc("How the screen presents, completing “Presented at %1/s, %2.” or a frame count", "tearing");
    case PresentationMode::AdaptiveAsync:
        return i18nc("How the screen presents, completing “Presented at %1/s, %2.” or a frame count", "adaptive sync with tearing");
    }
    return upscaleUnknownText();
}

// Frames as the screen showed them. An average alone hides the stutter that
// decides whether something feels smooth, so the slow tail is reported beside
// it, in the two forms that are both called a "one per cent low" and do not
// mean the same thing: the mean of the slowest hundredth, and the frame time
// that all but the slowest hundredth beat.
static QString presented(const UpscaleSnapshot &snapshot)
{
    if (snapshot.presentedRate < 0) {
        return i18n("Presented: %1", upscaleUnknownText());
    }
    QString text = i18n("Presented: %1/s average", QString::number(snapshot.presentedRate, 'f', 1));
    if (snapshot.presentedLow > 0) {
        // A literal percent sign, written once: KLocalizedString substitutes
        // numbered placeholders and does not collapse a doubled one the way
        // printf does, so "%%" would reach the screen as it stands here.
        text += i18nc("Appended to “Presented: %1/s average”", ", 1% low %1/s", QString::number(snapshot.presentedLow, 'f', 1));
    }
    if (snapshot.presentedPercentile > 0) {
        text += i18nc("Appended to “Presented: %1/s average”", ", 99th percentile %1 ms", QString::number(snapshot.presentedPercentile, 'f', 1));
    }
    if (snapshot.presentedWorst > 0) {
        text += i18nc("Appended to “Presented: %1/s average”", ", worst %1 ms", QString::number(snapshot.presentedWorst, 'f', 1));
    }
    return text + i18ncp("Appended to “Presented: %1/s average”; %2 is how the screen presents", " (%1 frame, %2)", " (%1 frames, %2)", snapshot.presentedFrames, presentationName(snapshot.presentation));
}

static QString measurement(const UpscaleSnapshot &snapshot)
{
    if (snapshot.clientUpdates < 0) {
        return i18n("Client buffer updates: %1", upscaleUnknownText());
    }
    // Name what is counted. A compositor repaint is not the game's frame rate,
    // so the two are reported separately and never merged into one "FPS".
    return i18n("Client buffer updates: %1/s, compositor repaints: %2/s (%3 s sample, %4 s ago)",
                QString::number(snapshot.clientUpdates, 'f', 1), QString::number(snapshot.repaints, 'f', 1),
                QString::number(snapshot.interval, 'f', 1), QString::number(snapshot.sampleAge, 'f', 1));
}

// How the buffer reached the compositor, for the display that carries the
// formats. It is as close to "what did it render with" as a compositor gets:
// neither protocol carries the client's graphics API, and an OpenGL and a
// Vulkan client hand over the same kind of buffer, so naming one would be a
// guess rather than an observation.
static QString bufferArrival(const UpscaleSnapshot &snapshot)
{
    switch (snapshot.bufferKind) {
    case UpscaleBufferKind::Gpu:
        return i18nc("How the buffer reached the compositor, completing “buffer format %1 arrived %2”", "on the GPU");
    case UpscaleBufferKind::SharedMemory:
        return i18nc("How the buffer reached the compositor, completing “buffer format %1 arrived %2”", "through main memory");
    case UpscaleBufferKind::Unknown:
        break;
    }
    return upscaleUnknownText();
}

static QString desiredText(const UpscaleSnapshot &snapshot)
{
    if (snapshot.preset == ResolutionPreset::Native) {
        // Native asks the game for nothing smaller, so there is no wish to
        // report: whatever it commits is used, and enlarged if it is smaller.
        return i18n("%1 (use the supplied buffer)", presetName(snapshot.preset));
    }
    // A wish, not what the game drew: the method that asked for it and the
    // size that arrived are reported beside it.
    return i18n("%1, %2% of the destination (%3)", presetName(snapshot.preset),
                qRound(resolutionRatio(snapshot.preset, snapshot.percentage) * 100),
                upscaleSizeText(QSize(snapshot.desired.width, snapshot.desired.height)));
}

static QString selection(const UpscaleSnapshot &snapshot)
{
    QStringList states;
    states.append(snapshot.activeWindow ? i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "active") : i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "not active"));
    states.append(snapshot.fullScreen ? i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "fullscreen") : i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "not fullscreen"));
    states.append(snapshot.selected ? i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "selected") : i18nc("A window's state, one of three listed after “Selection:”, joined by commas", "not selected"));
    return states.join(QStringLiteral(", "));
}

// Which of the two enlargements a resized X11 window is getting. It is the
// first thing to know when the picture is right and the pointer is not: the
// effect's own mapping is the one this project can change.
static QString x11Presentation(const UpscaleSnapshot &snapshot)
{
    switch (snapshot.x11Presentation) {
    case UpscaleX11Presentation::Xwayland:
        return i18nc("How a resized X11 window is shown, after “; ” or in the X11 resize line", "presented by Xwayland's emulated mode");
    case UpscaleX11Presentation::Effect:
        return i18nc("How a resized X11 window is shown, after “; ” or in the X11 resize line", "presented by this effect, pointer input mapped");
    case UpscaleX11Presentation::None:
        break;
    }
    return QString();
}

// What was asked of the game, and how: the first half of the "Desired" line.
static QString wishText(const UpscaleSnapshot &snapshot)
{
    const QString name = snapshot.recognized.isEmpty() ? application(snapshot) : snapshot.recognized;
    const QString width = QString::number(snapshot.desired.width);
    const QString height = QString::number(snapshot.desired.height);
    if (snapshot.requested.isValid()) {
        if (!upscaleIsX11(snapshot.presentedAs)) {
            return i18n("%1 requested from %2 as its Wayland window size", upscaleSizeText(snapshot.requested), name);
        }
        return i18n("%1 requested from %2 as its X11 window size", upscaleSizeText(snapshot.requested), name);
    }
    if (snapshot.scaleRequested > 0) {
        // Asked of the window's surface, and like an advertisement only a
        // request: the committed input below says what the client did. Named
        // before an advertisement, because the surface is asked only where the
        // advertisement did not reach the window, so this is the request the
        // window is answering.
        return i18n("%1 × %2 requested from %3 as its surface scale", width, height, name);
    }
    if (snapshot.advertised.isValid()) {
        // Advertised, not applied. The committed input below is the only
        // evidence of what the application actually did with it.
        //
        // An advertisement is said when the client starts and cannot be taken
        // back, so a wish that has moved on since is waiting for the next
        // start, and saying anything else would present a setting as a result.
        // A method that tells a scale reaches only whole steps, so a size told
        // that differs from the wish can also be the nearest step to it.
        const QString told = upscaleSizeText(snapshot.advertised);
        if (snapshot.preset == ResolutionPreset::Native) {
            return i18n("Native from the next start; %1 was told %2 as its screen mode", name, told);
        }
        if (snapshot.advertised == QSize(snapshot.desired.width, snapshot.desired.height)) {
            return i18n("%1 requested from %2 as its screen mode", told, name);
        }
        if (snapshot.nearestReachable) {
            return i18n("%1 requested from %2 as its screen mode, the nearest to %3 × %4 it can be told", told, name, width, height);
        }
        return i18n("%1 × %2 from the next start; %3 was told %4 as its screen mode", width, height, name, told);
    }
    if (snapshot.preset == ResolutionPreset::Native) {
        return i18n("Native (no request)");
    }
    if (upscaleIsAdvertisement(snapshot.method) && snapshot.advertisableAtStart) {
        // Nothing was said when this client started, and an advertisement can
        // be said only then.
        return i18n("%1 × %2 from the next start of %3", width, height, name);
    }
    return i18n("Select %1 × %2 in the game", width, height);
}

// The filter at work, and how the picture lies where it leaves bars.
static QString pictureText(const UpscaleSnapshot &snapshot)
{
    QString filter = snapshot.filter == UpscaleFilter::Nearest ? i18n("Nearest neighbor")
                                                               : i18n("FSR 1, sharpening %1%", qRound(snapshot.sharpening * 100));
    if (snapshot.factor > 0) {
        return i18np("%2, enlarged %1 time", "%2, enlarged %1 times", snapshot.factor, filter);
    }
    if (!snapshot.picture.isEmpty() && snapshot.picture.size() != snapshot.destination) {
        return i18n("%1, fitted into %2 with bars", filter, upscaleSizeText(snapshot.picture.size()));
    }
    return filter;
}

// What the effect is doing with the window.
static QString stateText(const UpscaleSnapshot &snapshot)
{
    QString state;
    if (snapshot.selected) {
        if (snapshot.scaling) {
            state = pictureText(snapshot);
        } else if (snapshot.refusal != UpscaleRefusal::None) {
            // A selected window carries the reason its last frame was handed
            // back, which describes that frame rather than the window.
            state = i18nc("%1 is a reason, written as a clause", "Eligible buffer; the last frame was not scaled because %1", refusalText(snapshot));
        } else {
            state = i18n("Eligible buffer; waiting for a compatible render pass.");
        }
    } else {
        state = i18nc("%1 is a reason, written as a clause", "Inactive: %1", refusalText(snapshot));
    }
    return state;
}

QString upscaleStatusText(const UpscaleSnapshot &snapshot)
{
    QString wish = wishText(snapshot);
    if (!snapshot.requestFailure.isEmpty()) {
        wish += i18nc("%1 is a reason, written as a clause", "; request failed: %1", snapshot.requestFailure);
    }
    if (const QString presentedBy = x11Presentation(snapshot); !presentedBy.isEmpty()) {
        wish += i18n("; %1", presentedBy);
    }
    const QString state = stateText(snapshot);
    // The rate decides this, not the mode. A screen that is replaced clears
    // its frame statistics without clearing the last mode it presented in, so
    // a mode can outlive the measurement it belonged to and this would
    // otherwise report a rate of minus one per second.
    const QString presentation = snapshot.presentedRate < 0
        ? i18n("Nothing has been presented on this screen yet.")
        : i18n("Presented at %1/s, %2.", QString::number(snapshot.presentedRate, 'f', 1),
               presentationName(snapshot.presentation));
    QStringList lines;
    lines.append(i18n("Desired: %1", wish));
    lines.append(i18n("Supplied input: %1", upscaleSizeText(snapshot.supplied)));
    lines.append(i18n("Destination: %1", upscaleSizeText(snapshot.destination)));
    lines.append(state);
    lines.append(presentation);
    // The measurements the developer block draws on the screen, repeated here
    // in the text a script can read. Comparing two runs is done on frame times
    // to a decimal place, and photographing the display is not a way to
    // collect them; this is the same numbers through D-Bus. They appear only
    // once something has been measured, so the sentence above keeps saying
    // that nothing has rather than being contradicted by a row of zeroes.
    if (snapshot.presentedRate >= 0 || snapshot.clientUpdates >= 0) {
        lines.append(presented(snapshot));
        lines.append(measurement(snapshot));
    }
    lines.append(i18n("HDR follows KWin's color management."));
    lines.append(upscaleMetrics(snapshot));
    return lines.join(QLatin1Char('\n'));
}

static QString areaText(const UpscaleRectF &area)
{
    return i18nc("A rectangle, as position and size", "%1,%2 %3 × %4",
                 QString::number(area.x(), 'f', 1), QString::number(area.y(), 'f', 1),
                 QString::number(area.width(), 'f', 1), QString::number(area.height(), 'f', 1));
}

QString upscaleDeveloperInformation(const UpscaleSnapshot &snapshot)
{
    QStringList lines;
    lines.append(i18n("Build: %1", snapshot.build.isEmpty() ? upscaleUnknownText() : snapshot.build));
    // The KWin version is the one this plugin was compiled against. The
    // running compositor may be a different one, and this does not observe it.
    lines.append(i18n("Runtime: %1 build, %2, built against KWin %3, Qt %4", snapshot.buildType, snapshot.graphics,
                      QString(KWIN_VERSION_STRING), QString::fromLatin1(qVersion())));
    lines.append(i18n("Window: %1 (%2) on %3", application(snapshot),
                      snapshot.application.isEmpty() ? upscaleUnknownText() : snapshot.application,
                      snapshot.output.isEmpty() ? upscaleUnknownText() : snapshot.output));
    lines.append(i18n("Selection: %1, %2", selection(snapshot), processing(snapshot)));
    lines.append(i18n("Application: %1, method %2, advertised %3",
                      snapshot.recognized.isEmpty() ? i18nc("No catalogue entry matched, completing “Application: %1”", "not recognized") : snapshot.recognized,
                      describeControlMethod(snapshot.method),
                      snapshot.advertised.isValid() ? upscaleSizeText(snapshot.advertised) : i18nc("No size was advertised, completing “advertised %1”", "nothing")));
    if (snapshot.method == UpscaleMethod::X11Resize) {
        const QString presentedBy = x11Presentation(snapshot);
        lines.append(i18nc("%2 is a reason, written as a clause, or none reported", "X11 resize: requested %1, failure %2, %3", upscaleSizeText(snapshot.requested),
                           snapshot.requestFailure.isEmpty() ? i18nc("No failure, completing “failure %1”", "none reported") : snapshot.requestFailure,
                           presentedBy.isEmpty() ? i18nc("How a resized X11 window is shown, after “; ” or in the X11 resize line", "not presented") : presentedBy));
    }
    lines.append(i18n("Configuration: %1, desired %2, sharpening %3",
                      snapshot.enabled ? i18nc("The effect's state, completing “Configuration: %1”", "enabled") : i18nc("The effect's state, completing “Configuration: %1”", "disabled"), desiredText(snapshot),
                      snapshot.sharpening > 0 ? i18n("RCAS %1%", qRound(snapshot.sharpening * 100)) : i18nc("No sharpening, completing “sharpening %1”", "off")));
    lines.append(i18n("Coverage: window %1, output %2", areaText(snapshot.windowArea),
                      areaText(snapshot.outputArea)));
    lines.append(i18n("Geometry: supplied %1, destination %2, output scale %3",
                      upscaleSizeText(snapshot.supplied), upscaleSizeText(snapshot.destination),
                      QString::number(snapshot.outputScale, 'f', 2)));
    lines.append(measurement(snapshot));
    // The frames the screen actually showed, and the mode it showed them in.
    // Whether variable refresh was in use is read from that mode, so this view
    // no longer has to say the question is unanswered.
    lines.append(presented(snapshot));
    lines.append(i18n("Frame: render target orientation %1, largest texture this GPU allows %2",
                      snapshot.targetTransform < 0 ? upscaleUnknownText() : QString::number(snapshot.targetTransform),
                      snapshot.maximumTexture > 0 ? QString::number(snapshot.maximumTexture) : upscaleUnknownText()));
    lines.append(i18n("Processing: buffer format %1 arrived %2, resources %3, scanout blocked by this effect: %4",
                      snapshot.format.isEmpty() ? upscaleUnknownText() : snapshot.format, bufferArrival(snapshot),
                      snapshot.failed ? i18nc("The scaler's resources, completing “resources %1”", "failed") : i18nc("The scaler's resources, completing “resources %1”", "ready"),
                      snapshot.blocksScanout ? i18nc("Whether this effect blocks direct scanout", "yes") : i18nc("Whether this effect blocks direct scanout", "no")));
    // Only the destination colour description is observed here.
    lines.append(i18n("Color: destination transfer %1, reference luminance %2 cd/m²",
                      transferName(snapshot.transferFunction),
                      snapshot.referenceLuminance > 0 ? QString::number(snapshot.referenceLuminance, 'f', 0) : upscaleUnknownText()));
    return lines.join(QLatin1Char('\n'));
}

} // namespace KWin
