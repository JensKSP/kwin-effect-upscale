/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The cases about what was requested of an application, apart from the rest of
// the snapshot test only because together they passed the file size limit.
// They are members of the same test class and run in the same executable.

#include "snapshot_test.h"

#include "warningtext.h"

using namespace KWin;

// What the effect asked an application for and what that application actually
// committed are two different observations. Presenting the request as the
// result is exactly the mistake this reporting exists to prevent.
void UpscaleSnapshotTest::separatesWhatWasRequestedFromWhatArrived()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.recognized = QStringLiteral("SuperTuxKart");
    snapshot.method = KWin::UpscaleMethod::AdvertisedMode;
    snapshot.preset = ResolutionPreset::Quality;
    snapshot.desired = {2560, 1440};
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

    snapshot.method = KWin::UpscaleMethod::X11Resize;
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
    nameless.preset = ResolutionPreset::Performance;
    nameless.desired = {1920, 1080};
    nameless.advertised = QSize(1920, 1080);
    QVERIFY2(upscaleStatusText(nameless).contains(QStringLiteral("1920 × 1080 requested from vkmark")),
             qPrintable(upscaleStatusText(nameless)));
}

// An advertisement is said when the client starts and cannot be taken back. A
// wish that changed while the game runs is waiting for its next start, and the
// report must say so rather than name the old request as if it were current.
void UpscaleSnapshotTest::reportsAWishThatWaitsForTheNextStart()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.recognized = QStringLiteral("SuperTuxKart");
    snapshot.method = KWin::UpscaleMethod::AdvertisedMode;
    snapshot.advertised = QSize(2560, 1440);
    snapshot.supplied = QSize(2560, 1440);
    snapshot.preset = ResolutionPreset::Performance;
    snapshot.desired = {1920, 1080};
    QString status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("1920 × 1080 from the next start; SuperTuxKart was told 2560 × 1440 as its screen mode")),
             qPrintable(status));
    QVERIFY(!status.contains(QStringLiteral("requested from")));

    snapshot.preset = ResolutionPreset::Native;
    status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("Native from the next start; SuperTuxKart was told 2560 × 1440 as its screen mode")),
             qPrintable(status));

    // Told nothing when it started, because the wish was Native then.
    snapshot.advertised = {};
    snapshot.advertisableAtStart = true;
    snapshot.preset = ResolutionPreset::Quality;
    snapshot.desired = {2560, 1440};
    status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("2560 × 1440 from the next start of SuperTuxKart")), qPrintable(status));
    QVERIFY(!status.contains(QStringLiteral("in the game")));

    // An entry that is not known to match before the window exists cannot be
    // said at the next start either, so the next start is not promised.
    snapshot.advertisableAtStart = false;
    status = upscaleStatusText(snapshot);
    QVERIFY2(!status.contains(QStringLiteral("next start")), qPrintable(status));
}

// Where the advertisement did not reach the window, the surface is asked
// instead, and that is the request the window is answering: the report names
// it rather than the advertisement it ignored.
void UpscaleSnapshotTest::namesTheSurfaceScaleOverAnIgnoredAdvertisement()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.recognized = QStringLiteral("SuperTuxKart");
    snapshot.method = KWin::UpscaleMethod::AdvertisedMode;
    snapshot.advertised = QSize(2560, 1440);
    snapshot.preset = ResolutionPreset::Performance;
    snapshot.desired = {1920, 1080};
    snapshot.scaleRequested = 0.5;
    const QString status = upscaleStatusText(snapshot);
    QVERIFY2(status.contains(QStringLiteral("1920 × 1080 requested from SuperTuxKart as its surface scale")),
             qPrintable(status));
    QVERIFY(!status.contains(QStringLiteral("screen mode")));
}

// A game that draws at a size of its own rather than the one chosen for it -
// one it stored from an earlier run, say - shows that size in the warning
// colour on the display. One that took the size, to within the 120ths the
// fractional scale travels in, shows it plainly.
void UpscaleSnapshotTest::marksASizeTheGameDidNotTake()
{
    UpscaleSnapshot snapshot = scaling();
    snapshot.destination = QSize(3840, 2160);
    snapshot.desired = {2560, 1440};
    snapshot.supplied = QSize(1920, 1080);
    QVERIFY2(upscaleHeadsUp(snapshot).contains(upscaleWarning(QStringLiteral("1080p"))), qPrintable(upscaleHeadsUp(snapshot)));
    QVERIFY(upscaleBasicSummary(snapshot).contains(upscaleWarningStart));
    // The status is not drawn by the overlay and never carries the marks.
    QVERIFY(!upscaleStatusText(snapshot).contains(upscaleWarningStart));

    snapshot.supplied = QSize(2560, 1440);
    QVERIFY(!upscaleHeadsUp(snapshot).contains(upscaleWarningStart));
    QVERIFY(!upscaleBasicSummary(snapshot).contains(upscaleWarningStart));

    // Balanced as the fractional scale delivers it: 71/120 of 3840 is 2272,
    // where 2259 was computed.
    snapshot.desired = {2259, 1271};
    snapshot.supplied = QSize(2272, 1278);
    QVERIFY(!upscaleHeadsUp(snapshot).contains(upscaleWarningStart));
}
