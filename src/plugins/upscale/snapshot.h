/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "eligibility.h"
#include "resolution.h"

#include <QSize>
#include <QString>

namespace KWin
{

/**
 * One coherent description of what the effect is doing right now.
 *
 * Settings, the on-screen display and the logs have to agree, and they cannot
 * agree while each of them reads the effect's members for itself at a
 * different moment. The effect fills this in one pass, so a report never
 * combines a new window's settings with the previous window's buffers.
 *
 * Every field is what was actually observed. Values that KWin does not expose,
 * or that belong to features not implemented yet, are left empty or invalid
 * and are reported as unknown rather than guessed.
 */
struct UpscaleSnapshot
{
    // Build and runtime
    QString build;
    QString buildType;
    QString graphics;

    // Selection
    QString window;
    QString application;
    QString output;
    bool selected = false;
    bool activeWindow = false;
    bool fullScreen = false;
    UpscaleRefusal refusal = UpscaleRefusal::None;
    bool scaling = false;
    bool blocksScanout = false;

    // Configuration
    bool enabled = true;
    ResolutionPreset preset = ResolutionPreset::Automatic;
    int percentage = 100;
    double sharpening = 0;

    // Geometry
    QSize supplied;
    QSize destination;
    UpscaleSize desired{0, 0};
    double outputScale = 1;

    // Processing
    QString format;
    bool failed = false;

    // Colour
    int transferFunction = -1;
    double referenceLuminance = 0;

    // Measurements. A negative rate means nothing has been sampled yet.
    double clientUpdates = -1;
    double repaints = -1;
    double interval = 0;
    double sampleAge = 0;
};

/**
 * The timed announcement naming the selected application. It never claims a
 * recognized game, because nothing in the effect identifies games yet.
 */
QString upscaleAnnouncement(const UpscaleSnapshot &snapshot);

/** The timed basic summary: observed sizes and the path actually taken. */
QString upscaleBasicSummary(const UpscaleSnapshot &snapshot);

/** The persistent statistics view. */
QString upscaleStatistics(const UpscaleSnapshot &snapshot);

/** The complete effective configuration and diagnostic state. */
QString upscaleDeveloperInformation(const UpscaleSnapshot &snapshot);

/** The settings page's status text, in the words that page has always used. */
QString upscaleStatusText(const UpscaleSnapshot &snapshot);

} // namespace KWin
