/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The advertisement case, apart from the rest of the integration test only
// because together they passed the file size limit. It is a member of the
// same test class and runs in the same session.

#include "integration_test.h"

void UpscaleIntegrationTest::asksApplicationsForASmallerImage()
{
    QTRY_VERIFY(m_effects.isValid());
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid());

    // Nothing is said while the request is switched off, whatever resolution
    // is preferred. The screen is 128 × 128, so its own mode is what arrives.
    configureResolution(false, true, Stored::Quality);
    {
        WaylandClient untouched;
        QVERIFY(untouched.initialize());
        QCOMPARE(untouched.advertisedMode(), QSize(128, 128));
    }

    // Quality is two thirds of the destination, which on this screen is 85.
    configureResolution(true, true, Stored::Quality);
    {
        WaylandClient asked;
        QVERIFY(asked.initialize());
        QCOMPARE(asked.advertisedMode(), QSize(85, 85));
        // The refresh rate stays the screen's own: only the size is in
        // question, and frame pacing is not this effect's to change.
        // Nothing asks this client for a scale, because an unscaled screen
        // offers a scale-driven client no whole step below one.
        QCOMPARE(asked.advertisedScale(), 1);
    }

    // A client that was already connected cannot be told anything: it read the
    // display information once, before this. Turning the request off has to
    // hand the screen's own mode back to the clients that were told otherwise,
    // so that the next thing to inspect these resources sees KWin's answer.
    {
        WaylandClient restored;
        QVERIFY(restored.initialize());
        QCOMPARE(restored.advertisedMode(), QSize(85, 85));
        configureResolution(false, true, Stored::Quality);
        QVERIFY(restored.roundtrip());
        QCOMPARE(restored.advertisedMode(), QSize(128, 128));
        QCOMPARE(restored.advertisedScale(), 1);
    }

    // Unlisted applications are off unless the user asks: nothing is known in
    // advance about a program nobody measured.
    configureResolution(true, false, Stored::Quality);
    {
        WaylandClient unlisted;
        QVERIFY(unlisted.initialize());
        QCOMPARE(unlisted.advertisedMode(), QSize(128, 128));
    }

    // A catalogue entry reaches the same client through its program's path,
    // which is the only identity that exists before it has a window.
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedMode\nResolution=Performance\nOrder=1\n")));
    {
        // A profile that states a resolution of its own gets it, whatever the
        // global one is: a key present in a profile is that game's answer, and
        // there is no negotiation between the two layers any more.
        configureResolution(true, false, Stored::Quality);
        WaylandClient known;
        QVERIFY(known.initialize());
        QCOMPARE(known.advertisedMode(), QSize(64, 64));
    }

    // A profile that states none follows the global resolution, which is how
    // a person's own global setting reaches the games this package ships.
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedMode\nOrder=1\n")));
    {
        configureResolution(true, false, Stored::Quality);
        WaylandClient chosen;
        QVERIFY(chosen.initialize());
        QCOMPARE(chosen.advertisedMode(), QSize(85, 85));

        // What was asked for is reported apart from what arrived: the client
        // is free to ignore the request, and the committed buffer is the only
        // evidence of what it did.
        QSocketNotifier notifier(chosen.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&chosen]() {
            chosen.dispatch();
        });
        QVERIFY(chosen.show(QSize(64, 64)));
        QTRY_VERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test")),
                     qPrintable(status()));
        QVERIFY2(status().contains(QStringLiteral("Supplied input: 64 × 64")), qPrintable(status()));

        // Another connection of this same executable must not overwrite the
        // selected window's advertisement, even on the same output.
        configureResolution(true, false, Stored::Performance);
        WaylandClient later;
        QVERIFY(later.initialize());
        QCOMPARE(later.advertisedMode(), QSize(64, 64));
        QVERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test")),
                 qPrintable(status()));

        // Changing a setting while the effect stays enabled moves what it
        // would ask for, so the resources of a client that was told otherwise
        // get KWin's own answer back. The client will not read them again, but
        // one that binds the output again, and anything that inspects them,
        // would otherwise see a mode this effect no longer asks for.
        configureResolution(true, false, Stored::Performance);
        QVERIFY(chosen.roundtrip());
        QCOMPARE(chosen.advertisedMode(), QSize(128, 128));
        // What that program was told is still what explains the size it is
        // rendering, so the report of it outlives the resources.
        QVERIFY2(status().contains(QStringLiteral("85 × 85 requested from Upscale integration test")),
                 qPrintable(status()));
    }

    // An entry that also names the window cannot be known to match before the
    // window exists. Its program is told nothing: not the entry's answer, and
    // not the global profile's either, because the program is not unlisted,
    // only undecided. Once the window exists the entry claims it, and what the
    // report asks of it is the entry's resolution, not the global one.
    writeCatalogue(integrationEntry(QStringLiteral("WindowClass=org.kde.upscale.integrationtest\n"
                                                   "MethodWaylandFullScreen=AdvertisedMode\nResolution=Performance\nOrder=1\n")));
    configureResolution(true, true, Stored::Quality);
    {
        WaylandClient undecided;
        QVERIFY(undecided.initialize());
        QCOMPARE(undecided.advertisedMode(), QSize(128, 128));
        QSocketNotifier notifier(undecided.descriptor(), QSocketNotifier::Read);
        connect(&notifier, &QSocketNotifier::activated, this, [&undecided]() {
            undecided.dispatch();
        });
        QVERIFY(undecided.show(QSize(64, 64)));
        QTRY_VERIFY2(status().contains(QStringLiteral("Select 64 × 64 in the game")), qPrintable(status()));
    }

    // X11 methods must never reach a native Wayland connection, even when the
    // process identity matches. None also recognizes without making a request.
    for (const QString &method : {QStringLiteral("Off"), QStringLiteral("X11Resize")}) {
        writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=%1\nOrder=1\n"))
                           .arg(method));
        WaylandClient silent;
        QVERIFY(silent.initialize());
        QCOMPARE(silent.advertisedMode(), QSize(128, 128));
    }

    // Native asks for the size the screen already has, which says nothing and
    // would leave the scaler nothing to enlarge either.
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedMode\nOrder=1\n")));
    configureResolution(true, false, Stored::Native);
    {
        WaylandClient native;
        QVERIFY(native.initialize());
        QCOMPARE(native.advertisedMode(), QSize(128, 128));
    }

    // A scale-driven client is moved only in whole steps of the output's own
    // scale. This screen has no scale above one, so it offers such a client
    // nothing at all, and the effect says nothing rather than asking for a
    // size the client would ignore.
    writeCatalogue(integrationEntry(QStringLiteral("MethodWaylandFullScreen=AdvertisedModeAndScale\nOrder=1\n")));
    configureResolution(true, false, Stored::Quality);
    {
        WaylandClient scaled;
        QVERIFY(scaled.initialize());
        QCOMPARE(scaled.advertisedMode(), QSize(128, 128));
        QCOMPARE(scaled.advertisedScale(), 1);
    }

    writeCatalogue(QString());
    configureResolution(true, false, {});
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
    QCOMPARE(status(), QString());
}
