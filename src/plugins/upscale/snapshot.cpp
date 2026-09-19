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
    // "Detected" for a window the effect merely picked, "recognized" only
    // where its identity matched the catalogue. A window that fills the
    // screen must not be presented as a match on that ground alone.
    if (snapshot.recognized.isEmpty()) {
        return i18n("Upscale: Detected %1", application(snapshot));
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
    return text + i18np(" (%1 frame, %2)", " (%1 frames, %2)", snapshot.presentedFrames, presentationName(snapshot.presentation));
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

// How a resolution is named where people compare them: by its common name
// where it has one, and by its pixels where it does not. "4K" is what a
// television and a graphics setting call 3840 x 2160, and writing it out is
// what makes this line readable at a glance rather than a row of numbers.
static QString resolutionName(const QSize &size)
{
    if (!size.isValid() || size.isEmpty()) {
        return unknown();
    }
    if (size == QSize(3840, 2160)) {
        return i18n("4K");
    }
    if (size == QSize(2560, 1440)) {
        return i18n("1440p");
    }
    if (size == QSize(1920, 1080)) {
        return i18n("1080p");
    }
    if (size == QSize(1280, 720)) {
        return i18n("720p");
    }
    // Equal height does not imply equal resolution, especially on ultrawide
    // outputs. Keep both dimensions when no common name describes this size.
    return sizeText(size);
}

// What share of the destination the game is actually drawing, in the linear
// per-axis terms every upscaler states its presets in: FSR 1 Quality is
// two thirds, not the four ninths of the pixels that implies.
static QString renderScale(const UpscaleSnapshot &snapshot)
{
    if (snapshot.supplied.width() <= 0 || snapshot.destination.width() <= 0) {
        return QString();
    }
    return i18n("%1%", qRound(100.0 * snapshot.supplied.width() / snapshot.destination.width()));
}

// What the client is, in the fewest words that stay true. It sits on the
// persistent view because that is the first thing to check when a request had
// no effect: a game running through Xwayland cannot be reached by a Wayland
// method, and that is invisible in every other figure there.
//
// "GPU" and "memory" describe how the buffer arrived, not what drew it. The
// graphics API is not observable from a compositor, so it is not claimed.
static QString clientKind(const UpscaleSnapshot &snapshot)
{
    switch (snapshot.windowSystem) {
    case UpscaleWindowSystem::Wayland:
        return i18n("Wayland");
    case UpscaleWindowSystem::X11:
        return i18n("X11");
    case UpscaleWindowSystem::Unknown:
        break;
    }
    return unknown();
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
        return i18n("on the GPU");
    case UpscaleBufferKind::SharedMemory:
        return i18n("through main memory");
    case UpscaleBufferKind::Unknown:
        break;
    }
    return unknown();
}

QString upscaleHeadsUp(const UpscaleSnapshot &snapshot)
{
    QStringList figures;
    // The rate is the screen's, the frame time is the game's, each taken from
    // the side where it still means something. A screen cannot present more
    // often than it refreshes, so above the refresh the presented rate stops
    // answering what a resolution costs, while the interval between the
    // buffers the game commits keeps answering it. Measured on 2026-09-19 at
    // 3840 x 2160 on a 240 Hz screen: SuperTuxKart presented 237/s at native,
    // quality and performance alike, and drew 493, 833 and 949 frames a second.
    // The recent window, not every frame held, so the figure answers for now.
    // It falls back to the whole window for a caller that fills a snapshot
    // without the statistics behind it, such as the settings page.
    const double rate = snapshot.presentedRecent > 0 ? snapshot.presentedRecent : snapshot.presentedRate;
    if (rate > 0) {
        figures.append(i18n("%1 FPS", QString::number(rate, 'f', 0)));
    } else {
        // A dash is what an overlay shows before it has measured anything. It
        // is not a zero, and it is not last minute's rate.
        figures.append(i18n("— FPS"));
    }
    // "1% low" is the name this figure carries everywhere it is quoted: the
    // mean of the slowest hundredth of the frames, as a rate.
    if (snapshot.presentedLow > 0) {
        figures.append(i18nc("The mean of the slowest hundredth of the frames, as a rate",
                             "1% low %1", QString::number(snapshot.presentedLow, 'f', 0)));
    } else {
        figures.append(i18nc("The mean of the slowest hundredth of the frames, as a rate", "1% low —"));
    }
    if (snapshot.clientUpdates > 0) {
        figures.append(i18nc("Milliseconds per frame the game drew: its frame time",
                             "%1 ms/f", QString::number(1000.0 / snapshot.clientUpdates, 'f', 1)));
    } else {
        figures.append(i18nc("Milliseconds per frame the game drew: its frame time", "— ms/f"));
    }
    QStringList picture;
    if (snapshot.scaling) {
        picture.append(snapshot.sharpening > 0 ? i18n("FSR 1 + RCAS") : i18n("FSR 1"));
        picture.append(i18n("%1 → %2", resolutionName(snapshot.supplied), resolutionName(snapshot.destination)));
        const QString scale = renderScale(snapshot);
        if (!scale.isEmpty()) {
            picture.append(scale);
        }
    } else if (!snapshot.destination.isEmpty() && snapshot.supplied == snapshot.destination) {
        // Native describes the observed buffer size. Bypassing FSR can still
        // leave KWin enlarging a smaller buffer, so bypass alone is not native.
        picture.append(i18n("%1 native", resolutionName(snapshot.destination)));
    } else if (!snapshot.destination.isEmpty()) {
        picture.append(i18n("FSR off"));
        picture.append(i18n("%1 → %2", resolutionName(snapshot.supplied), resolutionName(snapshot.destination)));
    }
    // On the picture line rather than a line of its own: it belongs with what
    // is being drawn, and a block read at a glance mid-game earns no third row
    // for it.
    picture.append(clientKind(snapshot));
    const QString separator = QStringLiteral("   ");
    QString text = figures.join(separator);
    if (!picture.isEmpty()) {
        text += QLatin1Char('\n') + picture.join(separator);
    }
    return text;
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

// Which of the two enlargements a resized X11 window is getting. It is the
// first thing to know when the picture is right and the pointer is not: the
// effect's own mapping is the one this project can change.
static QString x11Presentation(const UpscaleSnapshot &snapshot)
{
    switch (snapshot.x11Presentation) {
    case UpscaleX11Presentation::Xwayland:
        return i18n("presented by Xwayland's emulated mode");
    case UpscaleX11Presentation::Effect:
        return i18n("presented by this effect, pointer input mapped");
    case UpscaleX11Presentation::None:
        break;
    }
    return QString();
}

QString upscaleStatusText(const UpscaleSnapshot &snapshot)
{
    QString wish;
    if (snapshot.requested.isValid()) {
        wish = i18n("%1 requested from %2 as its X11 window size", sizeText(snapshot.requested),
                    snapshot.recognized.isEmpty() ? application(snapshot) : snapshot.recognized);
    } else if (snapshot.advertised.isValid()) {
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
    if (!snapshot.requestFailure.isEmpty()) {
        wish += i18n("; request failed: %1", snapshot.requestFailure);
    }
    if (const QString presentedBy = x11Presentation(snapshot); !presentedBy.isEmpty()) {
        wish += i18n("; %1", presentedBy);
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
    lines.append(i18n("Supplied input: %1", sizeText(snapshot.supplied)));
    lines.append(i18n("Destination: %1", sizeText(snapshot.destination)));
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
    lines.append(i18n("HDR follows KWin colour management."));
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
    if (snapshot.method == UpscaleControlMethod::X11Resize) {
        const QString presentedBy = x11Presentation(snapshot);
        lines.append(i18n("X11 resize: requested %1, failure %2, %3", sizeText(snapshot.requested),
                          snapshot.requestFailure.isEmpty() ? i18n("none reported") : snapshot.requestFailure,
                          presentedBy.isEmpty() ? i18n("not presented") : presentedBy));
    }
    lines.append(i18n("Configuration: %1, desired %2, sharpening %3",
                      snapshot.enabled ? i18n("enabled") : i18n("disabled"), desiredText(snapshot),
                      snapshot.sharpening > 0 ? i18n("RCAS %1%", qRound(snapshot.sharpening * 100)) : i18n("off")));
    lines.append(i18n("Coverage: window %1, output %2", areaText(snapshot.windowArea),
                      areaText(snapshot.outputArea)));
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
    lines.append(i18n("Processing: buffer format %1 arrived %2, resources %3, scanout blocked by this effect: %4",
                      snapshot.format.isEmpty() ? unknown() : snapshot.format, bufferArrival(snapshot),
                      snapshot.failed ? i18n("failed") : i18n("ready"),
                      snapshot.blocksScanout ? i18n("yes") : i18n("no")));
    // Only the destination colour description is observed here.
    lines.append(i18n("Colour: destination transfer %1, reference luminance %2 cd/m²",
                      transferName(snapshot.transferFunction),
                      snapshot.referenceLuminance > 0 ? QString::number(snapshot.referenceLuminance, 'f', 0) : unknown()));
    return lines.join(QLatin1Char('\n'));
}

} // namespace KWin
