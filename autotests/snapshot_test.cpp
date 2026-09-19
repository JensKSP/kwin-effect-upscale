/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

#include "effect/globals.h"

#include <QLocale>
#include <QSet>
#include <QTest>

using namespace KWin;

// The settings page, the on-screen display and the logs all read these texts.
// What they say is a contract with the person reading them, so it is tested
// here rather than left to whatever the formatting happens to produce.
class UpscaleSnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyRefusalHasItsOwnSentence();
    void unsupportedFormat();
    void refusalNamesTheConditionThatFailed();
    void reportsThePathActuallyTaken();
    void headsUpDistinguishesBypassFromNative();
    void headsUpKeepsUnusualDimensions();
    void doesNotInventUnknownValues();
    void pixelSizesAreNotGrouped();
    void developerInformationCoversTheState();
    void namesEveryPresetAndTransferFunction();
    void reportsPresentedFramesAndTheirSlowTail();
    void namesTheClientItIsLookingAt();
    void separatesWhatWasRequestedFromWhatArrived();

private:
    static UpscaleSnapshot scaling();
};

UpscaleSnapshot UpscaleSnapshotTest::scaling()
{
    UpscaleSnapshot snapshot;
    snapshot.build = QStringLiteral("upscale 0.1.0 (branch test), built now");
    snapshot.buildType = QStringLiteral("Debug");
    snapshot.graphics = QStringLiteral("OpenGL");
    snapshot.window = QStringLiteral("Tux Racer");
    snapshot.application = QStringLiteral("etr");
    snapshot.output = QStringLiteral("HDMI-A-1");
    snapshot.selected = true;
    snapshot.activeWindow = true;
    snapshot.fullScreen = true;
    snapshot.scaling = true;
    snapshot.blocksScanout = true;
    snapshot.sharpening = 0.5;
    snapshot.supplied = QSize(1280, 720);
    snapshot.destination = QSize(3840, 2160);
    snapshot.outputScale = 1;
    snapshot.format = QStringLiteral("XR24 (0x34325258)");
    snapshot.transferFunction = int(TransferFunction::gamma22);
    snapshot.referenceLuminance = 203;
    snapshot.clientUpdates = 59.8;
    snapshot.repaints = 60.1;
    snapshot.interval = 1;
    snapshot.sampleAge = 0.2;
    return snapshot;
}

void UpscaleSnapshotTest::everyRefusalHasItsOwnSentence()
{
    // The compiler already requires the switch to handle every value. This
    // requires each one to say something, and something different: two
    // conditions sharing a sentence would be two conditions nobody can tell
    // apart, which is the whole failure this reporting exists to fix.
    QSet<QString> seen;
    for (int reason = int(UpscaleRefusal::None); reason <= int(UpscaleRefusal::TransformedRenderTarget); ++reason) {
        const QString description = describeRefusal(static_cast<UpscaleRefusal>(reason));
        if (reason == int(UpscaleRefusal::None)) {
            QVERIFY(description.isEmpty());
            continue;
        }
        QVERIFY2(!description.isEmpty(), qPrintable(QStringLiteral("reason %1 has no description").arg(reason)));
        QVERIFY2(!seen.contains(description), qPrintable(description));
        seen.insert(description);
    }
    QVERIFY(seen.size() > 30);
}

void UpscaleSnapshotTest::refusalNamesTheConditionThatFailed()
{
    // A refused window must be reported by what failed, not by the rule.
    QVERIFY(describeRefusal(UpscaleRefusal::BufferNotSmaller).contains(QStringLiteral("not smaller than the destination")));
    QVERIFY(describeRefusal(UpscaleRefusal::BufferBelowHalf).contains(QStringLiteral("less than half")));
    QVERIFY(describeRefusal(UpscaleRefusal::BufferAspectRatio).contains(QStringLiteral("aspect ratio")));
    QVERIFY(describeRefusal(UpscaleRefusal::NotFullScreen).contains(QStringLiteral("not fullscreen")));
    QVERIFY(describeRefusal(UpscaleRefusal::SeveralCandidates).contains(QStringLiteral("more than one")));
    QVERIFY(describeRefusal(UpscaleRefusal::ChildSurfaces).contains(QStringLiteral("child surfaces")));

    UpscaleSnapshot snapshot = scaling();
    snapshot.selected = false;
    snapshot.scaling = false;
    snapshot.refusal = UpscaleRefusal::TranslucentContent;
    const QString status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("Inactive: the supplied buffer is not fully opaque")), qPrintable(status));
    QVERIFY(upscaleDeveloperInformation(snapshot).contains(QStringLiteral("not scaling: the supplied buffer is not fully opaque")));
    // The buffer format is named beside the refusal that was about the format.
    snapshot.refusal = UpscaleRefusal::UnsupportedBufferFormat;
    QVERIFY(upscaleStatusText(snapshot).contains(QStringLiteral("Supplied format: XR24 (0x34325258)")));
}

void UpscaleSnapshotTest::reportsThePathActuallyTaken()
{
    const UpscaleSnapshot snapshot = scaling();
    const QString developerPath = upscaleDeveloperInformation(snapshot);
    QVERIFY2(developerPath.contains(QStringLiteral("FSR 1 with RCAS sharpening 50%")), qPrintable(developerPath));
    QVERIFY(developerPath.contains(QStringLiteral("1280 × 720")));
    QVERIFY(developerPath.contains(QStringLiteral("3840 × 2160")));
    QVERIFY(developerPath.contains(QStringLiteral("HDMI-A-1")));
    // What a player turns the heads-up display on for is the frame rate, so it
    // carries that and the picture it was drawn at, and nothing that needs
    // explaining before it means anything.
    const QString headsUp = upscaleHeadsUp(snapshot);
    QVERIFY2(headsUp.contains(QStringLiteral("FSR 1 + RCAS")), qPrintable(headsUp));
    QVERIFY2(headsUp.contains(QStringLiteral("720p → 4K")), qPrintable(headsUp));
    QVERIFY2(headsUp.contains(QStringLiteral("33%")), qPrintable(headsUp));
    QVERIFY(!headsUp.contains(QStringLiteral("Client buffer updates")));
    QVERIFY(!headsUp.contains(QStringLiteral("compositor repaints")));
    QVERIFY(!headsUp.contains(QStringLiteral("percentile")));
    // Five lines of diagnostics are not a heads-up display.
    QCOMPARE(headsUp.count(QLatin1Char('\n')), 1);

    // The two counters are named by what they count. A compositor repaint is
    // not the game's frame rate and must never be presented as one.
    const QString developer = upscaleDeveloperInformation(snapshot);
    QVERIFY2(developer.contains(QStringLiteral("Client buffer updates: 59.8/s")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("compositor repaints: 60.1/s")));
    QVERIFY(developer.contains(QStringLiteral("1.0 s sample")));

    // The announcement names the application without claiming it was matched
    // against anything, because nothing identifies games yet.
    const QString announcement = upscaleAnnouncement(snapshot);
    QVERIFY2(announcement.contains(QStringLiteral("Detected Tux Racer")), qPrintable(announcement));
    QVERIFY(!announcement.contains(QStringLiteral("recognized")));
    QVERIFY(upscaleBasicSummary(snapshot).contains(QStringLiteral("1280 × 720 → 3840 × 2160")));

    UpscaleSnapshot eligible = snapshot;
    eligible.scaling = false;
    QVERIFY(upscaleStatusText(eligible).contains(QStringLiteral("waiting for a compatible render pass")));
    eligible.refusal = UpscaleRefusal::TransformedPass;
    QVERIFY(upscaleStatusText(eligible).contains(QStringLiteral("the last frame was not scaled because")));
    UpscaleSnapshot unsharpened = snapshot;
    unsharpened.sharpening = 0;
    QVERIFY(upscaleDeveloperInformation(unsharpened).contains(QStringLiteral("FSR 1, no sharpening")));
    // Sharpening changes the image, so the display that is always on screen
    // says whether it is on; it is not left to the diagnostic view.
    QVERIFY2(upscaleHeadsUp(unsharpened).contains(QStringLiteral("FSR 1   ")), qPrintable(upscaleHeadsUp(unsharpened)));
    QVERIFY(!upscaleHeadsUp(unsharpened).contains(QStringLiteral("RCAS")));
    // A game drawing at the size of the screen is not upscaled, and the
    // display says so rather than implying a benefit that is not there.
    UpscaleSnapshot native = snapshot;
    native.scaling = false;
    native.supplied = QSize(3840, 2160);
    QVERIFY2(upscaleHeadsUp(native).contains(QStringLiteral("4K native")), qPrintable(upscaleHeadsUp(native)));
}

void UpscaleSnapshotTest::doesNotInventUnknownValues()
{
    // A fresh snapshot has observed nothing. Every field it could not observe
    // has to say so rather than read as a measurement.
    const UpscaleSnapshot empty;
    const QString developer = upscaleDeveloperInformation(empty);
    QVERIFY2(developer.contains(QStringLiteral("Build: unknown")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("supplied unknown")));
    QVERIFY(upscaleDeveloperInformation(empty).contains(QStringLiteral("Client buffer updates: unknown")));
    // Nothing presented yet is said, not shown as a zero frame rate.
    // The heads-up display shows a dash before anything has been measured,
    // which is what an overlay shows for a figure it does not have yet.
    QVERIFY2(upscaleHeadsUp(empty).contains(QStringLiteral("— FPS")), qPrintable(upscaleHeadsUp(empty)));
    QVERIFY(upscaleHeadsUp(empty).contains(QStringLiteral("— ms")));
    QVERIFY(upscaleHeadsUp(empty).contains(QStringLiteral("1% Low — FPS")));
    QVERIFY(!upscaleHeadsUp(empty).contains(QStringLiteral("0 FPS")));
    QVERIFY(upscaleAnnouncement(empty).contains(QStringLiteral("unknown")));
    // An unimplemented or unobserved colour state is not filled in either.
    QVERIFY2(developer.contains(QStringLiteral("destination transfer unknown")), qPrintable(developer));
    // Nothing presented yet is said here as well, and the view no longer
    // claims variable refresh is unobservable now that the mode is measured.
    QVERIFY2(developer.contains(QStringLiteral("Presented: unknown")), qPrintable(developer));
    QVERIFY(!developer.contains(QStringLiteral("VRR not observed")));

    UpscaleSnapshot disabled;
    disabled.enabled = false;
    disabled.refusal = UpscaleRefusal::Disabled;
    QVERIFY(upscaleDeveloperInformation(disabled).contains(QStringLiteral("Configuration: disabled")));
    QVERIFY(upscaleStatusText(disabled).contains(QStringLiteral("Inactive: disabled")));
}

void UpscaleSnapshotTest::headsUpDistinguishesBypassFromNative()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.scaling = false;
    snapshot.refusal = UpscaleRefusal::TransformedPass;
    const QString bypass = upscaleHeadsUp(snapshot);
    QVERIFY2(bypass.contains(QStringLiteral("FSR off")), qPrintable(bypass));
    QVERIFY(bypass.contains(QStringLiteral("720p → 4K")));
    QVERIFY(!bypass.contains(QStringLiteral("native")));

    snapshot.supplied = snapshot.destination;
    QVERIFY(upscaleHeadsUp(snapshot).contains(QStringLiteral("4K native")));
    snapshot.supplied = QSize();
    const QString unknown = upscaleHeadsUp(snapshot);
    QVERIFY(!unknown.contains(QStringLiteral("native")));
    QVERIFY(unknown.contains(QStringLiteral("unknown → 4K")));
}

void UpscaleSnapshotTest::headsUpKeepsUnusualDimensions()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.supplied = QSize(1720, 720);
    snapshot.destination = QSize(5160, 2160);
    const QString ultrawide = upscaleHeadsUp(snapshot);
    QVERIFY2(ultrawide.contains(QStringLiteral("1720 × 720 → 5160 × 2160")), qPrintable(ultrawide));
    QVERIFY(!ultrawide.contains(QStringLiteral("4K")));
    QVERIFY(ultrawide.contains(QStringLiteral("33%")));

    snapshot.supplied = QSize(2560, 1440);
    snapshot.destination = QSize(3840, 2160);
    const QString quality = upscaleHeadsUp(snapshot);
    QVERIFY(quality.contains(QStringLiteral("1440p → 4K")));
    QVERIFY(quality.contains(QStringLiteral("67%")));
    snapshot.supplied = QSize(1920, 1080);
    QVERIFY(upscaleHeadsUp(snapshot).contains(QStringLiteral("1080p → 4K")));
}

void UpscaleSnapshotTest::pixelSizesAreNotGrouped()
{
    // A locale that groups thousands turned "3840 × 2160" into "3.840 × 2.160",
    // which reads as two fractional numbers rather than as a resolution.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const UpscaleSnapshot snapshot = scaling();
    const QString developer = upscaleDeveloperInformation(snapshot);
    QLocale::setDefault(previous);
    QVERIFY2(developer.contains(QStringLiteral("3840 × 2160")), qPrintable(developer));
    QVERIFY2(!developer.contains(QStringLiteral("3.840")), qPrintable(developer));
}

void UpscaleSnapshotTest::developerInformationCoversTheState()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.preset = ResolutionPreset::Quality;
    snapshot.percentage = 67;
    snapshot.desired = UpscaleSize{2560, 1440};
    const QString developer = upscaleDeveloperInformation(snapshot);
    for (const QString &group : {QStringLiteral("Build:"), QStringLiteral("Runtime:"), QStringLiteral("Window:"),
                                 QStringLiteral("Selection:"), QStringLiteral("Configuration:"), QStringLiteral("Geometry:"),
                                 QStringLiteral("Processing:"), QStringLiteral("Colour:")}) {
        QVERIFY2(developer.contains(group), qPrintable(group + QStringLiteral(" missing from:\n") + developer));
    }
    QVERIFY(developer.contains(QStringLiteral("active, fullscreen, selected")));
    QVERIFY2(developer.contains(QStringLiteral("Quality, 67% of the destination (2560 × 1440)")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("buffer format XR24")));
    QVERIFY(developer.contains(QStringLiteral("scanout blocked by this effect: yes")));
    QVERIFY(developer.contains(QStringLiteral("gamma 2.2")));
    QVERIFY(developer.contains(QStringLiteral("203")));

    // Automatic sends no request at all, and says so instead of naming a size.
    UpscaleSnapshot automatic = snapshot;
    automatic.preset = ResolutionPreset::Automatic;
    QVERIFY(upscaleDeveloperInformation(automatic).contains(QStringLiteral("use the supplied buffer")));
    QVERIFY(upscaleStatusText(automatic).contains(QStringLiteral("Automatic (no request)")));
    UpscaleSnapshot custom = snapshot;
    custom.preset = ResolutionPreset::Custom;
    QVERIFY(upscaleStatusText(custom).contains(QStringLiteral("Select 2560 × 1440 in the game")));
    UpscaleSnapshot failed = snapshot;
    failed.failed = true;
    QVERIFY(upscaleDeveloperInformation(failed).contains(QStringLiteral("resources failed")));
}

void UpscaleSnapshotTest::namesEveryPresetAndTransferFunction()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.desired = UpscaleSize{2560, 1440};
    // Find a group by its label rather than by position, so that adding a
    // group to the developer view does not silently move these assertions.
    const auto group = [](const QString &text, const QString &label) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (line.startsWith(label)) {
                return line;
            }
        }
        return QString();
    };
    QSet<QString> presets;
    for (const ResolutionPreset preset : {ResolutionPreset::Automatic, ResolutionPreset::Native,
                                          ResolutionPreset::UltraQuality, ResolutionPreset::Quality,
                                          ResolutionPreset::Balanced, ResolutionPreset::Performance,
                                          ResolutionPreset::Custom}) {
        snapshot.preset = preset;
        const QString line = group(upscaleDeveloperInformation(snapshot), QStringLiteral("Configuration:"));
        QVERIFY2(!line.isEmpty() && !line.contains(QStringLiteral("unknown")), qPrintable(line));
        presets.insert(line);
    }
    // Seven presets, seven different things said about them.
    QCOMPARE(presets.size(), 7);

    QSet<QString> transfers;
    for (const int transfer : {int(TransferFunction::sRGB), int(TransferFunction::linear),
                               int(TransferFunction::PerceptualQuantizer), int(TransferFunction::gamma22)}) {
        snapshot.transferFunction = transfer;
        const QString colour = group(upscaleDeveloperInformation(snapshot), QStringLiteral("Colour:"));
        QVERIFY2(!colour.isEmpty() && !colour.contains(QStringLiteral("transfer unknown")), qPrintable(colour));
        transfers.insert(colour);
    }
    QCOMPARE(transfers.size(), 4);
    // A transfer function this build does not know is reported as unknown
    // rather than decoded with the wrong curve's name.
    snapshot.transferFunction = 99;
    QVERIFY(upscaleDeveloperInformation(snapshot).contains(QStringLiteral("transfer unknown")));
}

// The frame rate is what a person turns this on for, and an average alone
// hides the stutter that decides whether something feels smooth. Every measure
// beside it has to be named by what it actually is.
void UpscaleSnapshotTest::namesTheClientItIsLookingAt()
{
    // The rectangles the coverage rule compares are reported as they are,
    // fractions included: on a fractionally scaled output the logical size is
    // not a whole number, and rounding it away would hide the difference this
    // line exists to explain.
    UpscaleSnapshot covered = scaling();
    covered.windowArea = UpscaleRectF(0, 0, 2560, 1440);
    covered.outputArea = UpscaleRectF(0, 0, 2648.28, 1489.66);
    const QString areas = upscaleDeveloperInformation(covered);
    QVERIFY2(areas.contains(QStringLiteral("window 0.0,0.0 2560.0 × 1440.0")), qPrintable(areas));
    QVERIFY2(areas.contains(QStringLiteral("output 0.0,0.0 2648.3 × 1489.7")), qPrintable(areas));

    // The first thing to check when a request had no effect is which window
    // system the client speaks: a Wayland method cannot reach an Xwayland
    // game, and no other figure on this block would ever show it.
    UpscaleSnapshot wayland = scaling();
    wayland.windowSystem = UpscaleWindowSystem::Wayland;
    wayland.bufferKind = UpscaleBufferKind::Gpu;
    const QString shown = upscaleHeadsUp(wayland);
    QVERIFY2(shown.contains(QStringLiteral("Wayland")), qPrintable(shown));
    // How the buffer arrived is buffer information, which the handbook keeps
    // out of the block read at a glance and puts with the formats instead.
    QVERIFY2(!shown.contains(QStringLiteral("GPU")), qPrintable(shown));
    const QString detail = upscaleDeveloperInformation(wayland);
    QVERIFY2(detail.contains(QStringLiteral("arrived on the GPU")), qPrintable(detail));

    UpscaleSnapshot x11 = scaling();
    x11.windowSystem = UpscaleWindowSystem::X11;
    x11.bufferKind = UpscaleBufferKind::SharedMemory;
    const QString other = upscaleHeadsUp(x11);
    QVERIFY2(other.contains(QStringLiteral("X11")), qPrintable(other));
    QVERIFY2(!other.contains(QStringLiteral("Wayland")), qPrintable(other));
    QVERIFY2(upscaleDeveloperInformation(x11).contains(QStringLiteral("through main memory")),
             qPrintable(upscaleDeveloperInformation(x11)));

    // Neither is claimed when neither was established. A guess here would be
    // read as a measurement, which is what this block is for.
    UpscaleSnapshot unknown = scaling();
    const QString silent = upscaleHeadsUp(unknown);
    QVERIFY2(!silent.contains(QStringLiteral("GPU")), qPrintable(silent));
    QVERIFY2(!silent.contains(QStringLiteral("Wayland")), qPrintable(silent));
}

void UpscaleSnapshotTest::reportsPresentedFramesAndTheirSlowTail()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.presentedRate = 59.94;
    snapshot.presentedLow = 41.2;
    snapshot.presentedPercentile = 28.35;
    snapshot.presentedWorst = 51.7;
    // One frame is one frame. The count carries a plural form so that a window
    // holding a single interval does not report "1 frames".
    snapshot.presentedFrames = 1;
    QVERIFY2(upscaleDeveloperInformation(snapshot).contains(QStringLiteral("1 frame,")),
             qPrintable(upscaleDeveloperInformation(snapshot)));
    snapshot.presentedFrames = 600;
    snapshot.presentation = int(PresentationMode::AdaptiveSync);
    const QString statistics = upscaleDeveloperInformation(snapshot);
    QVERIFY2(statistics.contains(QStringLiteral("Presented: 59.9/s average")), qPrintable(statistics));
    // The two figures both called a "one per cent low" do not mean the same
    // thing, so each is named by what it measures rather than by that phrase.
    QVERIFY2(statistics.contains(QStringLiteral("1% low 41.2/s")), qPrintable(statistics));
    QVERIFY(statistics.contains(QStringLiteral("99th percentile 28.4 ms")));
    QVERIFY(statistics.contains(QStringLiteral("worst 51.7 ms")));
    QVERIFY(statistics.contains(QStringLiteral("600 frames")));
    // Adaptive synchronisation is finally an observation rather than a
    // disclaimer, so the mode the screen presented in is reported with them.
    QVERIFY2(statistics.contains(QStringLiteral("adaptive sync")), qPrintable(statistics));
    // The same frames, as a player reads them: a whole number of frames a
    // second, the milliseconds one of them took, and the slow tail by the
    // name every overlay gives it.
    const QString glance = upscaleHeadsUp(snapshot);
    QVERIFY2(glance.contains(QStringLiteral("60 FPS")), qPrintable(glance));
    QVERIFY2(glance.contains(QStringLiteral("16.7 ms")), qPrintable(glance));
    QVERIFY2(glance.contains(QStringLiteral("1% Low 41 FPS")), qPrintable(glance));
    QVERIFY(upscaleStatusText(snapshot).contains(QStringLiteral("Presented at 59.9/s, adaptive sync.")));

    // Each presentation mode has to be named, and named differently: two modes
    // sharing a word would be two modes nobody can tell apart.
    QSet<QString> modes;
    for (const PresentationMode mode : {PresentationMode::VSync, PresentationMode::AdaptiveSync,
                                        PresentationMode::Async, PresentationMode::AdaptiveAsync}) {
        snapshot.presentation = int(mode);
        // Only the phrase naming the mode is under test. The rest of this view
        // says "unknown" for everything this snapshot never observed, which is
        // the behaviour asserted elsewhere.
        const QString named = upscaleDeveloperInformation(snapshot)
                                  .section(QLatin1String("frames, "), 1)
                                  .section(QLatin1Char('\n'), 0, 0);
        QVERIFY2(!named.contains(QStringLiteral("unknown")), qPrintable(named));
        modes.insert(named);
    }
    QCOMPARE(modes.size(), 4);

    // A measure nothing produced is left out rather than shown as a zero, and
    // nothing presented at all is said rather than reported as no frames.
    UpscaleSnapshot partial = scaling();
    partial.presentedRate = 30;
    partial.presentedFrames = 12;
    partial.presentation = int(PresentationMode::VSync);
    const QString sparse = upscaleDeveloperInformation(partial);
    QVERIFY2(sparse.contains(QStringLiteral("Presented: 30.0/s average")), qPrintable(sparse));
    QVERIFY(!sparse.contains(QStringLiteral("1% low")));
    QVERIFY(!sparse.contains(QStringLiteral("percentile")));
    QVERIFY(!sparse.contains(QStringLiteral("worst")));
    QVERIFY(upscaleStatusText(scaling()).contains(QStringLiteral("Nothing has been presented")));

    // Replacing the screen clears its statistics without clearing the mode it
    // last presented in. The rate is what says whether anything was measured,
    // and a rate of minus one is not a frame rate.
    UpscaleSnapshot stale = scaling();
    stale.presentation = int(PresentationMode::AdaptiveSync);
    const QString restarted = upscaleStatusText(stale);
    QVERIFY2(restarted.contains(QStringLiteral("Nothing has been presented")), qPrintable(restarted));
    QVERIFY2(!restarted.contains(QStringLiteral("-1.0/s")), qPrintable(restarted));

    // A rate without a mode is the other way round: the figure is real and the
    // mode it was presented in is what is unknown.
    UpscaleSnapshot modeless = scaling();
    modeless.presentedRate = 60;
    QVERIFY2(upscaleStatusText(modeless).contains(QStringLiteral("Presented at 60.0/s, unknown.")),
             qPrintable(upscaleStatusText(modeless)));

    // The developer view reports the same frames rather than saying that
    // variable refresh cannot be observed at all.
    UpscaleSnapshot measured = scaling();
    measured.presentedRate = 59.94;
    measured.presentedFrames = 600;
    measured.presentation = int(PresentationMode::AdaptiveSync);
    const QString developer = upscaleDeveloperInformation(measured);
    QVERIFY2(developer.contains(QStringLiteral("Presented: 59.9/s average")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("adaptive sync")));
    QVERIFY(!developer.contains(QStringLiteral("VRR not observed")));
}

// What the effect asked an application for and what that application actually
// committed are two different observations. Presenting the request as the
// result is exactly the mistake this reporting exists to prevent.
void UpscaleSnapshotTest::separatesWhatWasRequestedFromWhatArrived()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.recognized = QStringLiteral("SuperTuxKart");
    snapshot.method = UpscaleControlMethod::AdvertisedMode;
    snapshot.advertised = QSize(2560, 1440);
    snapshot.supplied = QSize(3840, 2160);
    snapshot.destination = QSize(3840, 2160);

    // "recognized" is said only for a catalogue match, never for a window that
    // merely fills the screen.
    const QString announcement = upscaleAnnouncement(snapshot);
    QVERIFY2(announcement.contains(QStringLiteral("recognized SuperTuxKart")), qPrintable(announcement));
    QVERIFY(!announcement.contains(QStringLiteral("selected")));

    const QString status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("2560 × 1440 requested from SuperTuxKart as its screen mode")),
             qPrintable(status));
    // The committed buffer is reported beside it, and it is the only evidence
    // of what the application did with the request. Here it ignored it.
    QVERIFY(status.contains(QStringLiteral("Supplied input: 3840 × 2160")));

    const QString developer = upscaleDeveloperInformation(snapshot);
    QVERIFY2(developer.contains(QStringLiteral("Application: SuperTuxKart")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("advertised 2560 × 1440")));
    QVERIFY(developer.contains(QStringLiteral("advertised screen mode")));

    snapshot.method = UpscaleControlMethod::X11Resize;
    snapshot.advertised = {};
    snapshot.requested = QSize(1920, 1080);
    snapshot.requestFailure = QStringLiteral("The requested mode was ignored.");
    const QString refused = upscaleStatusText(snapshot);
    QVERIFY(refused.contains(QStringLiteral("1920 × 1080 requested from SuperTuxKart as its X11 window size")));
    QVERIFY(refused.contains(snapshot.requestFailure));
    QVERIFY(refused.contains(QStringLiteral("Supplied input: 3840 × 2160")));
    QVERIFY(!refused.contains(QStringLiteral("as its screen mode")));

    // A window nothing in the catalogue describes says so, rather than
    // reporting an empty name or implying a match.
    UpscaleSnapshot unlisted = scaling();
    QVERIFY(upscaleDeveloperInformation(unlisted).contains(QStringLiteral("not recognized")));
    QVERIFY(upscaleDeveloperInformation(unlisted).contains(QStringLiteral("advertised nothing")));
    // Nothing was requested, so the desired size is a wish for the user to act
    // on and must not be phrased as something that was asked for.
    QVERIFY(!upscaleStatusText(unlisted).contains(QStringLiteral("requested from")));

    // An application the effect recognized only by its program has no window
    // name to fall back on, and the request still has to name something.
    UpscaleSnapshot nameless;
    nameless.application = QStringLiteral("vkmark");
    nameless.advertised = QSize(1920, 1080);
    QVERIFY2(upscaleStatusText(nameless).contains(QStringLiteral("1920 × 1080 requested from vkmark")),
             qPrintable(upscaleStatusText(nameless)));
}

void UpscaleSnapshotTest::unsupportedFormat()
{
    UpscaleSnapshot state;
    state.refusal = UpscaleRefusal::UnsupportedBufferFormat;
    for (const QString &format : {QString(), QStringLiteral("NV12 (0x3231564e)")}) {
        state.format = format;
        const QString expected = QStringLiteral("Supplied format: %1.").arg(format.isEmpty() ? QStringLiteral("unknown") : format);
        for (bool selected : {false, true}) {
            state.selected = selected;
            QVERIFY(upscaleStatusText(state).contains(expected));
            QVERIFY(upscaleBasicSummary(state).contains(expected));

            QVERIFY(upscaleDeveloperInformation(state).contains(expected));
        }
    }
}

QTEST_GUILESS_MAIN(UpscaleSnapshotTest)

#include "snapshot_test.moc"
