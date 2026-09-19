/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
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
/** Which window system a client speaks, or that it could not be told. */
enum class UpscaleWindowSystem {
    Unknown,
    Wayland,
    X11,
};

struct UpscaleSnapshot
{
    // Build and runtime
    QString build;
    QString buildType;
    QString graphics;

    // Selection
    QString window;
    QString application;
    // The catalogue entry this window's identity matched, empty when the
    // effect does not recognize it. A recognized application is not a claim
    // that anything was done for it: the method says that.
    QString recognized;
    UpscaleControlMethod method = UpscaleControlMethod::None;
    // The size this effect told the application its screen has, invalid when
    // it told it nothing. Never confuse it with the committed buffer: the
    // application is free to ignore it.
    QSize advertised;
    // A live X11 request is a window size, not a Wayland mode advertisement.
    QSize requested;
    QString requestFailure;
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
    // The two rectangles the coverage rule compares. A window is only scaled
    // while it occupies its whole output, and when it is refused for that the
    // sizes are the first thing anyone needs to see.
    UpscaleRectF windowArea;
    UpscaleRectF outputArea;

    // What the client is, which is the first thing to know when a request had
    // no effect. The window system decides which road a request can travel at
    // all: an Xwayland client never binds the compositor's wl_output, so the
    // advertised-mode method cannot reach it however well it is configured.
    //
    // The graphics API a client renders with is deliberately absent. Nothing
    // in either protocol carries it and a compositor only ever sees buffers,
    // so the honest neighbour is how the buffer arrived: on the GPU or through
    // main memory. Naming OpenGL or Vulkan here would be a guess.
    UpscaleWindowSystem windowSystem = UpscaleWindowSystem::Unknown;
    UpscaleBufferKind bufferKind = UpscaleBufferKind::Unknown;

    // Processing
    QString format;
    bool failed = false;

    // The orientation of the frame being painted, as an OutputTransform kind,
    // and negative outside a paint pass. Reported because it decides whether a
    // frame can be replaced at all, and its value differs between backends.
    int targetTransform = -1;
    // What the driver said it can allocate, and zero when it has not been
    // asked. The scaler needs a texture at the destination size, so a
    // destination beyond this is the machine's limit rather than a defect.
    int maximumTexture = 0;

    // Colour
    int transferFunction = -1;
    double referenceLuminance = 0;

    // Measurements. A negative rate means nothing has been sampled yet.
    //
    // The presented figures describe frames the screen actually showed, which
    // is what a person sees and what hardware is compared on. They are kept
    // apart from the two counters below, which describe what the client
    // committed and what the compositor painted: a painted frame that was
    // dropped or repeated is not a frame anybody saw.
    double presentedRate = -1;
    // The mean of the slowest hundredth of frames, as a rate: "one per cent
    // low" in the sense a hardware review means it.
    double presentedLow = -1;
    // The frame time nine hundred and ninety-nine frames in a thousand beat.
    double presentedPercentile = -1;
    double presentedWorst = -1;
    int presentedFrames = 0;
    // How the screen presented them, as a PresentationMode, or negative when
    // nothing has been presented yet. This is the observation that tells
    // whether adaptive synchronisation was actually in use.
    int presentation = -1;

    double clientUpdates = -1;
    double repaints = -1;
    double interval = 0;
    double sampleAge = 0;
};

/**
 * The timed announcement naming the application. It says "recognized" only
 * for a catalogue match, and being recognized is never presented as proof
 * that the resolution request reached anything.
 */
QString upscaleAnnouncement(const UpscaleSnapshot &snapshot);

/** The timed basic summary: observed sizes and the path actually taken. */
QString upscaleBasicSummary(const UpscaleSnapshot &snapshot);

/**
 * The persistent heads-up display: the few figures a player reads at a glance
 * while playing, in the words every frame-rate overlay uses for them.
 *
 * Deliberately short and deliberately large. Everything that needs explaining
 * before it means anything — the counters, the percentiles, the formats, the
 * colour state — is developer information and belongs in that view.
 */
QString upscaleHeadsUp(const UpscaleSnapshot &snapshot);

/** The complete effective configuration and diagnostic state. */
QString upscaleDeveloperInformation(const UpscaleSnapshot &snapshot);

/** The settings page's status text, in the words that page has always used. */
QString upscaleStatusText(const UpscaleSnapshot &snapshot);

} // namespace KWin
