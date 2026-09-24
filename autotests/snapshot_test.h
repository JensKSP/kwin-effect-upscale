/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "snapshot.h"

#include <QObject>
#include <QTest>

// The settings page, the on-screen display and the logs all read these texts.
// What they say is a contract with the person reading them, so it is tested
// here rather than left to whatever the formatting happens to produce.
class UpscaleSnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void everyRefusalHasItsOwnSentence();
    void unsupportedFormat();
    void refusalNamesTheConditionThatFailed();
    void reportsThePathActuallyTaken();
    void doesNotInventUnknownValues();
    void pixelSizesAreNotGrouped();
    void developerInformationCoversTheState();
    void namesEveryPresetAndTransferFunction();
    void reportsPresentedFramesAndTheirSlowTail();
    void namesTheClientItIsLookingAt();
    void separatesWhatWasRequestedFromWhatArrived();
    void reportsAWishThatWaitsForTheNextStart();
    void namesTheSurfaceScaleOverAnIgnoredAdvertisement();
    void marksASizeTheGameDidNotTake();

private:
    static KWin::UpscaleSnapshot scaling();
};
