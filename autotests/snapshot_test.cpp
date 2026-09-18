/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

#include <QSet>
#include <QTest>

using namespace KWin;

class UpscaleSnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unknownObservations();
    void effectiveState();
    void refusalDescriptions();
    void unsupportedFormat();
    void requestedResolution_data();
    void requestedResolution();
    void destinationColors_data();
    void destinationColors();
};

void UpscaleSnapshotTest::unknownObservations()
{
    UpscaleSnapshot state;
    state.refusal = UpscaleRefusal::NoWindow;
    QCOMPARE(upscaleAnnouncement(state), QStringLiteral("Upscale: selected unknown"));
    QVERIFY(upscaleBasicSummary(state).contains(QStringLiteral("unknown → unknown")));
    const QString statistics = upscaleStatistics(state);
    QVERIFY(statistics.contains(QStringLiteral("Client buffer updates: unknown")));
    QVERIFY(!statistics.contains(QStringLiteral("0.0/s")));
    const QString information = upscaleDeveloperInformation(state);
    QVERIFY(information.contains(QStringLiteral("Build: unknown")));
    QVERIFY(information.contains(QStringLiteral("Window: unknown (unknown) on unknown")));
    QVERIFY(information.contains(QStringLiteral("not active, not fullscreen, not selected")));
    QVERIFY(information.contains(QStringLiteral("Automatic (use the supplied buffer)")));
    QVERIFY(information.contains(QStringLiteral("reference luminance unknown")));
    QVERIFY(information.contains(QStringLiteral("VRR not observed")));
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("Inactive: there is no window to scale")));
}

void UpscaleSnapshotTest::effectiveState()
{
    UpscaleSnapshot state;
    state.build = QStringLiteral("candidate-123");
    state.buildType = QStringLiteral("Debug");
    state.graphics = QStringLiteral("OpenGL ES");
    state.application = QStringLiteral("test.application");
    QCOMPARE(upscaleAnnouncement(state), QStringLiteral("Upscale: selected test.application"));
    state.window = QStringLiteral("Test window");
    state.output = QStringLiteral("Display 1");
    state.selected = true;
    state.activeWindow = true;
    state.fullScreen = true;
    state.supplied = QSize(1920, 1080);
    state.destination = QSize(3840, 2160);
    state.format = QStringLiteral("XR24 (0x34325258)");
    QCOMPARE(upscaleAnnouncement(state), QStringLiteral("Upscale: selected Test window"));
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("waiting for a compatible render pass")));
    QVERIFY(upscaleBasicSummary(state).contains(QStringLiteral("not scaling yet")));

    state.refusal = UpscaleRefusal::TranslucentPass;
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("last frame was not scaled because")));
    QVERIFY(upscaleBasicSummary(state).contains(QStringLiteral("reduced opacity")));
    QVERIFY(!upscaleBasicSummary(state).contains(QStringLiteral("FSR 1")));
    state.refusal = UpscaleRefusal::None;
    state.scaling = true;
    QCOMPARE(upscaleBasicSummary(state), QStringLiteral("1920 × 1080 → 3840 × 2160, FSR 1, no sharpening"));
    state.sharpening = 0.5;
    QVERIFY(upscaleBasicSummary(state).contains(QStringLiteral("RCAS sharpening 50%")));
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("FSR 1, sharpening 50%")));

    state.clientUpdates = 120;
    state.repaints = 60;
    state.interval = 2;
    state.sampleAge = 0.5;
    const QString statistics = upscaleStatistics(state);
    QVERIFY(statistics.contains(QStringLiteral("on Display 1")));
    QVERIFY(statistics.contains(QStringLiteral("Client buffer updates: 120.0/s, compositor repaints: 60.0/s (2.0 s sample, 0.5 s ago)")));
    const QString information = upscaleDeveloperInformation(state);
    QVERIFY(information.contains(QStringLiteral("Build: candidate-123")));
    QVERIFY(information.contains(QStringLiteral("Debug build, OpenGL ES")));
    QVERIFY(information.contains(QStringLiteral("active, fullscreen, selected")));
    QVERIFY(information.contains(QStringLiteral("sharpening RCAS 50%")));
    QVERIFY(information.contains(QStringLiteral("buffer format XR24 (0x34325258), resources ready")));

    state.enabled = false;
    state.failed = true;
    state.blocksScanout = true;
    state.scaling = false;
    state.selected = false;
    state.refusal = UpscaleRefusal::UnsupportedBufferFormat;
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("Supplied format: XR24 (0x34325258)")));
    const QString failed = upscaleDeveloperInformation(state);
    QVERIFY(failed.contains(QStringLiteral("Configuration: disabled")));
    QVERIFY(failed.contains(QStringLiteral("resources failed, scanout blocked by this effect: yes")));
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
            QVERIFY(upscaleStatistics(state).contains(expected));
            QVERIFY(upscaleDeveloperInformation(state).contains(expected));
        }
    }
}

void UpscaleSnapshotTest::refusalDescriptions()
{
    // Distinct conditions must remain distinguishable in every user-facing
    // report. Checking only whether a refusal produced any text would allow
    // the old generic eligibility sentence to hide the failing condition.
    QSet<QString> descriptions;
    for (int value = int(UpscaleRefusal::Disabled); value <= int(UpscaleRefusal::TransformedRenderTarget); ++value) {
        const UpscaleRefusal refusal = static_cast<UpscaleRefusal>(value);
        const QString description = describeRefusal(refusal);
        QVERIFY2(!description.isEmpty(), qPrintable(QString::number(value)));
        QVERIFY2(!descriptions.contains(description), qPrintable(description));
        descriptions.insert(description);
        UpscaleSnapshot state;
        state.refusal = refusal;
        QVERIFY(upscaleStatusText(state).contains(description));
        QVERIFY(upscaleBasicSummary(state).contains(description));
        QVERIFY(!upscaleBasicSummary(state).contains(QStringLiteral("FSR 1")));
    }
    QVERIFY(describeRefusal(UpscaleRefusal::None).isEmpty());
}

void UpscaleSnapshotTest::requestedResolution_data()
{
    QTest::addColumn<int>("preset");
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("percentage");
    QTest::newRow("native") << int(ResolutionPreset::Native) << QStringLiteral("Native") << 100;
    QTest::newRow("ultra") << int(ResolutionPreset::UltraQuality) << QStringLiteral("Ultra Quality") << 77;
    QTest::newRow("quality") << int(ResolutionPreset::Quality) << QStringLiteral("Quality") << 67;
    QTest::newRow("balanced") << int(ResolutionPreset::Balanced) << QStringLiteral("Balanced") << 59;
    QTest::newRow("performance") << int(ResolutionPreset::Performance) << QStringLiteral("Performance") << 50;
    QTest::newRow("custom") << int(ResolutionPreset::Custom) << QStringLiteral("Custom") << 73;
}

void UpscaleSnapshotTest::requestedResolution()
{
    QFETCH(int, preset);
    QFETCH(QString, name);
    QFETCH(int, percentage);
    UpscaleSnapshot state;
    state.preset = static_cast<ResolutionPreset>(preset);
    state.percentage = percentage;
    state.desired = {1280, 720};
    state.supplied = QSize(1600, 900);
    const QString information = upscaleDeveloperInformation(state);
    QVERIFY(information.contains(name + QStringLiteral(", %1% of the destination (1280 × 720)").arg(percentage)));
    QVERIFY(information.contains(QStringLiteral("supplied 1600 × 900")));
    QVERIFY(upscaleStatusText(state).contains(QStringLiteral("Select 1280 × 720 in the game")));
    QVERIFY(!upscaleBasicSummary(state).contains(QStringLiteral("1280 × 720")));
}

void UpscaleSnapshotTest::destinationColors_data()
{
    QTest::addColumn<int>("transfer");
    QTest::addColumn<QString>("name");
    QTest::newRow("srgb") << int(TransferFunction::sRGB) << QStringLiteral("sRGB");
    QTest::newRow("linear") << int(TransferFunction::linear) << QStringLiteral("linear");
    QTest::newRow("pq") << int(TransferFunction::PerceptualQuantizer) << QStringLiteral("PQ");
    QTest::newRow("gamma") << int(TransferFunction::gamma22) << QStringLiteral("gamma 2.2");
    QTest::newRow("unobserved") << -1 << QStringLiteral("unknown");
}

void UpscaleSnapshotTest::destinationColors()
{
    QFETCH(int, transfer);
    QFETCH(QString, name);
    UpscaleSnapshot state;
    state.transferFunction = transfer;
    state.referenceLuminance = 203;
    const QString information = upscaleDeveloperInformation(state);
    QVERIFY(information.contains(QStringLiteral("destination transfer ") + name));
    QVERIFY(information.contains(QStringLiteral("reference luminance 203 cd/m², VRR not observed")));
}

QTEST_GUILESS_MAIN(UpscaleSnapshotTest)

#include "snapshot_test.moc"
