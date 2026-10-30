/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The cases about which scaling filter drew, apart from the rest of the
// snapshot test only because together they passed the file size limit. They
// are members of the same test class and run in the same executable.

#include "snapshot_test.h"

#include "filtertext.h"

using namespace KWin;

// Every filter is named as what ran, with the sharpener that sharpened it: RCAS
// after it, its own, or none.
void UpscaleSnapshotTest::namesEachFilterWithItsSharpener()
{
    // Another filter is named with the RCAS that follows it, or without.
    UpscaleSnapshot lanczos = scaling();
    lanczos.filter = UpscaleFilter::Lanczos;
    QVERIFY2(upscaleDeveloperInformation(lanczos).contains(QStringLiteral("Lanczos with RCAS sharpening 50%"))
                 && upscaleDeveloperInformation(lanczos).contains(QStringLiteral("sharpening RCAS 50%")),
             qPrintable(upscaleDeveloperInformation(lanczos)));
    QVERIFY2(upscaleHeadsUp(lanczos).contains(QStringLiteral("Lanczos + RCAS")), qPrintable(upscaleHeadsUp(lanczos)));
    lanczos.filter = UpscaleFilter::Bilinear;
    lanczos.sharpening = 0;
    QVERIFY2(upscaleDeveloperInformation(lanczos).contains(QStringLiteral("bilinear, no sharpening"))
                 && upscaleDeveloperInformation(lanczos).contains(QStringLiteral("sharpening off")),
             qPrintable(upscaleDeveloperInformation(lanczos)));
    QVERIFY2(upscaleHeadsUp(lanczos).contains(QStringLiteral("Bilinear")) && !upscaleHeadsUp(lanczos).contains(QStringLiteral("RCAS")),
             qPrintable(upscaleHeadsUp(lanczos)));
    // A filter that sharpens itself is not said to be followed by RCAS, and
    // its own sharpener is named instead.
    UpscaleSnapshot gsr = scaling();
    gsr.filter = UpscaleFilter::Gsr;
    QVERIFY2(upscaleDeveloperInformation(gsr).contains(QStringLiteral("Snapdragon GSR 1 with its own sharpening 50%"))
                 && upscaleDeveloperInformation(gsr).contains(QStringLiteral("sharpening GSR 50%"))
                 && !upscaleDeveloperInformation(gsr).contains(QStringLiteral("RCAS")),
             qPrintable(upscaleDeveloperInformation(gsr)));
    QVERIFY2(upscaleHeadsUp(gsr).contains(QStringLiteral("Snapdragon GSR 1, sharpened")) && !upscaleHeadsUp(gsr).contains(QStringLiteral("RCAS")),
             qPrintable(upscaleHeadsUp(gsr)));
    gsr.filter = UpscaleFilter::Nis;
    QVERIFY2(upscaleDeveloperInformation(gsr).contains(QStringLiteral("NVIDIA Image Scaling with its own sharpening 50%"))
                 && upscaleDeveloperInformation(gsr).contains(QStringLiteral("sharpening NIS 50%"))
                 && !upscaleDeveloperInformation(gsr).contains(QStringLiteral("RCAS")),
             qPrintable(upscaleDeveloperInformation(gsr)));
    // xBRZ never sharpens, and nothing says it does.
    gsr.filter = UpscaleFilter::Xbrz;
    gsr.sharpening = 0;
    const QString xbrz = upscaleDeveloperInformation(gsr);
    QVERIFY2(xbrz.contains(QStringLiteral("xBRZ")) && !xbrz.contains(QStringLiteral("xBRZ with")) && !xbrz.contains(QStringLiteral("xBRZ, no")),
             qPrintable(xbrz));
}

// Which filter drew is part of what a measurement measured, and a program
// reads it by the name kwinupscalerc gives it, never by the translated one.
// Nothing is named where nothing was scaled.
void UpscaleSnapshotTest::metricsNameTheFilterThatDrew()
{
    UpscaleSnapshot snapshot = scaling();
    for (int value = 0; value <= int(upscaleLastFilter); ++value) {
        snapshot.filter = UpscaleFilter(value);
        const QString metrics = upscaleMetrics(snapshot);
        QVERIFY2(metrics.contains(QStringLiteral(" filter=") + upscaleFilterKey(snapshot.filter) + QLatin1Char(' ')), qPrintable(metrics));
    }
    QVERIFY(upscaleMetrics(snapshot).contains(QStringLiteral(" filter=xBRZ ")));
    snapshot.scaling = false;
    QVERIFY(!upscaleMetrics(snapshot).contains(QStringLiteral("filter=")));
}
