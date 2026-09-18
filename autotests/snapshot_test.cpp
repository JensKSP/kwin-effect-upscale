/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

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
    void refusalNamesTheConditionThatFailed();
    void reportsThePathActuallyTaken();
    void doesNotInventUnknownValues();
    void pixelSizesAreNotGrouped();
    void developerInformationCoversTheState();
    void namesEveryPresetAndTransferFunction();

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
    QVERIFY(upscaleStatistics(snapshot).contains(QStringLiteral("not scaling: the supplied buffer is not fully opaque")));
    // The buffer format is named beside the refusal that was about the format.
    snapshot.refusal = UpscaleRefusal::UnsupportedBufferFormat;
    QVERIFY(upscaleStatusText(snapshot).contains(QStringLiteral("Supplied format: XR24 (0x34325258)")));
}

void UpscaleSnapshotTest::reportsThePathActuallyTaken()
{
    const UpscaleSnapshot snapshot = scaling();
    const QString statistics = upscaleStatistics(snapshot);
    QVERIFY2(statistics.contains(QStringLiteral("FSR 1 with RCAS sharpening 50%")), qPrintable(statistics));
    QVERIFY(statistics.contains(QStringLiteral("1280 × 720")));
    QVERIFY(statistics.contains(QStringLiteral("3840 × 2160")));
    QVERIFY(statistics.contains(QStringLiteral("HDMI-A-1")));
    // The two counters are named by what they count. A compositor repaint is
    // not the game's frame rate and must never be presented as one.
    QVERIFY(statistics.contains(QStringLiteral("Client buffer updates: 59.8/s")));
    QVERIFY(statistics.contains(QStringLiteral("compositor repaints: 60.1/s")));
    QVERIFY(statistics.contains(QStringLiteral("1.0 s sample")));

    // The announcement names the application without claiming it was matched
    // against anything, because nothing identifies games yet.
    const QString announcement = upscaleAnnouncement(snapshot);
    QVERIFY2(announcement.contains(QStringLiteral("selected Tux Racer")), qPrintable(announcement));
    QVERIFY(!announcement.contains(QStringLiteral("recognized")));
    QVERIFY(upscaleBasicSummary(snapshot).contains(QStringLiteral("1280 × 720 → 3840 × 2160")));

    UpscaleSnapshot eligible = snapshot;
    eligible.scaling = false;
    QVERIFY(upscaleStatusText(eligible).contains(QStringLiteral("waiting for a compatible render pass")));
    eligible.refusal = UpscaleRefusal::TransformedPass;
    QVERIFY(upscaleStatusText(eligible).contains(QStringLiteral("the last frame was not scaled because")));
    UpscaleSnapshot unsharpened = snapshot;
    unsharpened.sharpening = 0;
    QVERIFY(upscaleStatistics(unsharpened).contains(QStringLiteral("FSR 1, no sharpening")));
}

void UpscaleSnapshotTest::doesNotInventUnknownValues()
{
    // A fresh snapshot has observed nothing. Every field it could not observe
    // has to say so rather than read as a measurement.
    const UpscaleSnapshot empty;
    const QString developer = upscaleDeveloperInformation(empty);
    QVERIFY2(developer.contains(QStringLiteral("Build: unknown")), qPrintable(developer));
    QVERIFY(developer.contains(QStringLiteral("supplied unknown")));
    QVERIFY(upscaleStatistics(empty).contains(QStringLiteral("Client buffer updates: unknown")));
    QVERIFY(upscaleAnnouncement(empty).contains(QStringLiteral("unknown")));
    // An unimplemented or unobserved colour state is not filled in either.
    QVERIFY(developer.contains(QStringLiteral("VRR not observed")));

    UpscaleSnapshot disabled;
    disabled.enabled = false;
    disabled.refusal = UpscaleRefusal::Disabled;
    QVERIFY(upscaleDeveloperInformation(disabled).contains(QStringLiteral("Configuration: disabled")));
    QVERIFY(upscaleStatusText(disabled).contains(QStringLiteral("Inactive: disabled")));
}

void UpscaleSnapshotTest::pixelSizesAreNotGrouped()
{
    // A locale that groups thousands turned "3840 × 2160" into "3.840 × 2.160",
    // which reads as two fractional numbers rather than as a resolution.
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const UpscaleSnapshot snapshot = scaling();
    const QString statistics = upscaleStatistics(snapshot);
    QLocale::setDefault(previous);
    QVERIFY2(statistics.contains(QStringLiteral("3840 × 2160")), qPrintable(statistics));
    QVERIFY2(!statistics.contains(QStringLiteral("3.840")), qPrintable(statistics));
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

QTEST_GUILESS_MAIN(UpscaleSnapshotTest)

#include "snapshot_test.moc"
