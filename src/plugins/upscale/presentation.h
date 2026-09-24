/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace KWin
{

/**
 * The ways a game can present itself, each of which may need a different
 * request.
 *
 * Called presentations rather than modes because AdvertisedMode already means
 * a display mode, and the two appearing in one sentence is how a reader ends
 * up believing the effect changes the screen's mode.
 *
 * The protocol half is a property of the client and the shape half is a
 * property of the window, and neither predicts the other: the same game
 * launched twice can arrive in different cells. That is the whole reason a
 * profile answers per cell instead of once.
 */
enum class UpscalePresentation {
    WaylandFullScreen,
    WaylandBorderless,
    WaylandWindowed,
    X11FullScreen,
    X11Borderless,
    X11Windowed,
};

inline constexpr std::size_t upscalePresentationCount = std::size_t(UpscalePresentation::X11Windowed) + 1;

/**
 * What the effect says to a program presenting one way.
 *
 * Auto is a method in its own right and not a choice among the others. On X11
 * it resizes, verifies that the window still covers its output and that the
 * pointer still lands where it looks, and puts the size back where it does
 * not. On Wayland it tells the program a smaller screen mode when the program
 * connects, which is the only thing SDL's exclusive fullscreen ever reads, and
 * once the window exists asks a surface still drawing at full size for a
 * fractional scale, the only lever that can be sent after the window and taken
 * back again. It means the same whether an entry or the global profile
 * answers; KWin's own clients, Xwayland above all, are never told anything.
 *
 * Off is not the same answer as Auto. Off records that the question was asked
 * about this program and that asking it anything is pointless or harmful;
 * Auto, which is what an absent key resolves to, records that nobody has run
 * the program that way yet.
 */
enum class UpscaleMethod {
    Auto,
    Off,
    AdvertisedMode,
    AdvertisedScale,
    AdvertisedModeAndScale,
    X11Resize,
};

/** Whether @p presentation is one an X11 client can arrive in. */
constexpr bool upscaleIsX11(UpscalePresentation presentation)
{
    return presentation == UpscalePresentation::X11FullScreen
        || presentation == UpscalePresentation::X11Borderless
        || presentation == UpscalePresentation::X11Windowed;
}

/** Whether @p presentation is a window the user sized, rather than a screen. */
constexpr bool upscaleIsWindowed(UpscalePresentation presentation)
{
    return presentation == UpscalePresentation::WaylandWindowed
        || presentation == UpscalePresentation::X11Windowed;
}

/**
 * Whether @p method is one @p presentation can carry.
 *
 * Each method acts on exactly one protocol: the advertisements act on
 * wl_output, which an Xwayland game never sees because it reaches the
 * compositor through Xwayland's own connection, and the resize acts on an X11
 * window. A windowed presentation carries neither yet: obtaining a smaller
 * buffer there means holding the window's size while the client renders below
 * it, which no implemented path does.
 */
constexpr bool upscaleMethodApplies(UpscalePresentation presentation, UpscaleMethod method)
{
    switch (method) {
    case UpscaleMethod::Auto:
    case UpscaleMethod::Off:
        return true;
    case UpscaleMethod::X11Resize:
        return upscaleIsX11(presentation) && !upscaleIsWindowed(presentation);
    default:
        return !upscaleIsX11(presentation) && !upscaleIsWindowed(presentation);
    }
}

/** Whether @p method is one of the three said when a client binds its output. */
constexpr bool upscaleIsAdvertisement(UpscaleMethod method)
{
    return method == UpscaleMethod::AdvertisedMode || method == UpscaleMethod::AdvertisedScale
        || method == UpscaleMethod::AdvertisedModeAndScale;
}

/**
 * The presentation whose method is said when a client binds the output.
 *
 * An advertisement has to be made before the client has a window, and the
 * presentation is not knowable then, so a profile whose two Wayland answers
 * differ still has to resolve to one statement. It is the fullscreen one, and
 * that is a reasoned choice rather than a coin toss:
 *
 *  - a borderless client of the configure-sized kind, which is most of them,
 *    takes its size from the configure the compositor sends and is unaffected
 *    by a falsified mode either way;
 *  - the kind that is harmed reads its size as a plain window, which is the
 *    windowed presentation, and Auto never acts on those;
 *  - the remaining kind is Wine, which cannot be matched at bind at all,
 *    because the connection belongs to the Wine loader and the game's own name
 *    arrives later as the window's app_id.
 *
 * So the fullscreen answer is the only one that can both help and be said in
 * time. A borderless answer naming an advertisement is kept, because it
 * records a measurement, and the settings page says it cannot be made at bind
 * rather than pretending it was applied.
 */
constexpr UpscalePresentation upscaleAdvertisedPresentation()
{
    return UpscalePresentation::WaylandFullScreen;
}

/**
 * The cell a window is in, from the three things that decide it.
 *
 * Taken as answers rather than as a window, because the callers hold different
 * kinds of window - an effect's, the compositor's, an X11 one - and none of
 * them should have to agree on a type to ask the same question. The fullscreen
 * state is asked before the geometry deliberately: a window this effect made
 * smaller still holds that state, and it is still presenting full screen.
 */
constexpr UpscalePresentation upscalePresentationFor(bool x11, bool fullScreen, bool borderlessOverOutput)
{
    if (fullScreen) {
        return x11 ? UpscalePresentation::X11FullScreen : UpscalePresentation::WaylandFullScreen;
    }
    if (borderlessOverOutput) {
        return x11 ? UpscalePresentation::X11Borderless : UpscalePresentation::WaylandBorderless;
    }
    return x11 ? UpscalePresentation::X11Windowed : UpscalePresentation::WaylandWindowed;
}

/** An answer for each presentation, as the global profile gives them. */
using UpscaleMethods = std::array<UpscaleMethod, upscalePresentationCount>;

/** What a game states for each presentation; absent follows its parent. */
using UpscaleStatedMethods = std::array<std::optional<UpscaleMethod>, upscalePresentationCount>;

} // namespace KWin
