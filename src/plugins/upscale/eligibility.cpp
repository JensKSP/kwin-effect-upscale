/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "eligibility.h"

#include "application.h"
#include "resolution.h"
#include "upscaleconfig.h"

#include "effect/effecthandler.h"
#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "window.h"

#include <KLocalizedString>

#include <drm_fourcc.h>

#include <cmath>
#include <optional>

namespace KWin
{

static std::optional<uint32_t> bufferFormat(SurfaceItem *surface)
{
#if UPSCALE_REGION_API
    GraphicsBuffer *buffer = surface->buffer();
#else
    // This call is required, not an optimisation: KWin creates the surface
    // pixmap inside ItemRenderer::renderItem, which runs after the effect
    // chain, so nothing else has made the buffer reachable by the time an
    // effect first looks. Without it the effect is never eligible and never
    // reaches a paint pass that would create the pixmap.
    surface->updatePixmap();
    SurfacePixmap *pixmap = surface->pixmap();
    GraphicsBuffer *buffer = pixmap ? pixmap->buffer() : nullptr;
#endif
    if (!buffer) {
        return std::nullopt;
    }
    if (const DmaBufAttributes *dmaBuffer = buffer->dmabufAttributes()) {
        return dmaBuffer->format;
    }
    if (const ShmAttributes *shared = buffer->shmAttributes()) {
        return shared->format;
    }
    // A buffer of a kind this KWin exposes through neither accessor. It has no
    // format to report, and the scaler cannot assume one.
    return DRM_FORMAT_INVALID;
}

static bool readableFormat(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_ABGR2101010:
    // Sixteen bits per channel, as a Vulkan client asking for the best format
    // it can get will choose. The unsigned ones sample exactly like the eight
    // bit formats above, normalized to the same range, so the shaders need to
    // know nothing about them; refusing them only refused the client.
    case DRM_FORMAT_XRGB16161616:
    case DRM_FORMAT_ARGB16161616:
    case DRM_FORMAT_XBGR16161616:
    case DRM_FORMAT_ABGR16161616:
    case DRM_FORMAT_XRGB16161616F:
    case DRM_FORMAT_ARGB16161616F:
    case DRM_FORMAT_XBGR16161616F:
    case DRM_FORMAT_ABGR16161616F:
        return true;
    default:
        return false;
    }
}

// A window that was switched away from is still fullscreen: KWin leaves its
// state and its geometry exactly as they were and simply stops showing it.
// Nothing else below can tell that apart from the game being played, so
// without this the effect goes on scaling a window nobody can see, goes on
// holding the output in composition for it, and goes on drawing the display
// over whatever was raised in front of it.
//
// The rule is KWin's own for fullscreen windows, Window::isActiveFullScreen().
// That one is protected, so it is repeated here rather than called: the window
// counts while it holds the activation, while what holds it is one of its own
// dialogs, and while what holds it is on another screen and therefore in front
// of nothing here.
//
// One difference from KWin's is deliberate. KWin reads no active window at all
// as not active, because what it decides there is which layer to put the
// window in; that is not a statement that the user stopped looking at it.
// Reading the moment during a switch when nothing yet holds the activation as
// the game being gone would take the display off the screen and put it back.
static bool showingOnItsOutput(EffectWindow *window)
{
    EffectWindow *active = effects->activeWindow();
    if (!active || active == window || active->screen() != window->screen()) {
        return true;
    }
    Window *focused = active->window();
    Window *internal = window->window();
    return focused && internal && focused->allMainWindows().contains(internal);
}

// Logical geometry is fractional whenever the output's scale is. A 3840 x 2160
// output at scale 1.45 is 2648.28 x 1489.66 logical, and a client can only ever
// commit whole pixels, so no window can equal that rectangle exactly. Comparing
// the two as they stand therefore refuses a window that covers the screen
// completely: measured on 2026-09-19, a fullscreen SuperTuxKart whose image
// reached all four edges of the screen was refused for coverage, at the one
// desktop scale the earlier evidence had never been gathered at.
//
// The question is settled where the answer is defined, in the device pixels the
// scaler reads and writes. Two edges that round to the same pixel cover the same
// pixel, and nothing finer than a pixel can be drawn differently.
static bool samePixel(double first, double second, double scale)
{
    // Within one device pixel, rather than rounding each side and comparing.
    // Rounding on its own is not enough: two edges a fraction of a pixel apart
    // can still fall either side of a rounding boundary. Measured 2026-09-19
    // on a 3840 x 2160 output at scale 1.45, a fullscreen SuperTuxKart window
    // was 1490.3 logical high against an output 1489.7 high — 2160.9 device
    // pixels against 2160.1 — and was refused for a difference no pixel can
    // show and no frame can draw.
    return std::abs(first - second) * scale <= 1.0;
}

// Whether the window occupies its whole output. This is a gate, not a
// measurement of where to draw: the scaler is given the window's own geometry
// as its destination, so a window that passes here is one whose enlargement
// this effect may replace.
static bool coversOutput(EffectWindow *window)
{
    UpscaleOutput *screen = window->screen();
    if (!screen) {
        return false;
    }
    const double scale = screen->scale();
    const auto frame = window->frameGeometry();
    const auto output = screen->geometryF();
    // The far edges, not the dimensions. Rounding each of an origin and a
    // width to the output's values still permits their sum to land a pixel
    // short or a pixel over, which is a strip left uncovered or drawn past the
    // screen. Where the edges agree, every pixel between them is covered.
    return samePixel(frame.x(), output.x(), scale) && samePixel(frame.y(), output.y(), scale)
        && samePixel(frame.x() + frame.width(), output.x() + output.width(), scale)
        && samePixel(frame.y() + frame.height(), output.y() + output.height(), scale);
}

// The window itself: what it is and where it sits, before anything about its
// contents is examined. These checks are cheap and run for every window of
// every frame, so they stay in the order that rejects the common case first.
bool upscalePresentation(EffectWindow *window)
{
    if (window->isFullScreen()) {
        return true;
    }
    const Window *internal = window->window();
    // Borderless applications need not advertise fullscreen. Match both the
    // origin and extent of one output; equal dimensions on a different output
    // or a spanning window do not describe the same presentation. Requiring a
    // profile keeps ordinary desktop windows out of this additional path.
    return internal && internal->isNormalWindow() && !internal->isDecorated()
        && internal->clientGeometry() == internal->frameGeometry()
        && coversOutput(window)
        && upscaleApplicationForIdentity(internal->resourceClass(), internal->resourceName());
}

static UpscaleRefusal placementRefusal(EffectWindow *window)
{
    if (!upscalePresentation(window)) {
        return UpscaleRefusal::NotFullScreen;
    }
    if (window->isDeleted()) {
        return UpscaleRefusal::Closing;
    }
    if (window->isMinimized()) {
        return UpscaleRefusal::Minimized;
    }
    if (!window->isOnCurrentDesktop()) {
        return UpscaleRefusal::OtherDesktop;
    }
    if (!window->isOnCurrentActivity()) {
        return UpscaleRefusal::OtherActivity;
    }
    if (!showingOnItsOutput(window)) {
        return UpscaleRefusal::NotActive;
    }
    if (window->opacity() != 1.0) {
        return UpscaleRefusal::TranslucentWindow;
    }
    if (!window->screen()) {
        return UpscaleRefusal::NoOutput;
    }
    const Window *internal = window->window();
    const UpscaleApplication *application = internal ? upscaleApplicationForIdentity(internal->resourceClass(), internal->resourceName()) : nullptr;
    const ResolutionPreset preset = effectiveResolutionPreset(static_cast<ResolutionPreset>(UpscaleConfig::preset()),
                                                              application ? application->preset : ResolutionPreset::Automatic);
    if (preset == ResolutionPreset::Native) {
        return UpscaleRefusal::NativeRule;
    }
    const int minimum = application && application->minimumPixels >= 0 ? application->minimumPixels : UpscaleConfig::minimumPixels();
    const QSize pixels = window->screen()->pixelSize();
    if (!exceedsMinimumPixels({pixels.width(), pixels.height()}, minimum)) {
        return UpscaleRefusal::BelowMinimumPixels;
    }
    if (!window->windowItem() || !window->windowItem()->surfaceItem()) {
        return UpscaleRefusal::NoSurface;
    }
    if (window->screen()->transform() != OutputTransform::Normal) {
        return UpscaleRefusal::TransformedOutput;
    }
    if (!coversOutput(window)) {
        return UpscaleRefusal::NotCoveringOutput;
    }
    if (!window->windowItem()->transform().isIdentity()) {
        return UpscaleRefusal::TransformedWindow;
    }
    return UpscaleRefusal::None;
}

// The surface inside the window: it has to be the whole window's contents,
// undisplaced and unscaled, because the scaler replaces the enlargement step
// and can only do that for a surface KWin would otherwise enlarge by itself.
static UpscaleRefusal surfaceRefusal(EffectWindow *window, SurfaceItem *surface)
{
    if (!surface->childItems().isEmpty()) {
        return UpscaleRefusal::ChildSurfaces;
    }
    if (!surface->transform().isIdentity()) {
        return UpscaleRefusal::TransformedSurface;
    }
    if (surface->position() != QPointF()) {
        return UpscaleRefusal::OffsetSurface;
    }
    if (surface->opacity() != 1.0) {
        return UpscaleRefusal::TranslucentSurface;
    }
    // Compared in device pixels for the same reason as the coverage above: on a
    // fractionally scaled output the two can never agree exactly.
    const double scale = window->screen() ? window->screen()->scale() : 1;
    const QSizeF destination = surface->destinationSize();
    const auto frame = window->frameGeometry().size();
    if (!samePixel(destination.width(), frame.width(), scale)
        || !samePixel(destination.height(), frame.height(), scale)) {
        return UpscaleRefusal::ResizedSurface;
    }
    return UpscaleRefusal::None;
}

// The buffer the client supplied: its size relative to the destination, and
// whether the scaler can read it as it stands.
static UpscaleRefusal contentRefusal(EffectWindow *window, SurfaceItem *surface)
{
    const QSize input = surface->bufferSize();
    const QSize destination = window->screen()->pixelSize();
    switch (upscaleSizing({input.width(), input.height()}, {destination.width(), destination.height()})) {
    case UpscaleSizing::Supported:
        break;
    case UpscaleSizing::EmptyBuffer:
        // An empty size usually means no buffer has arrived yet rather than a
        // client that committed one of zero size. Say which it is, because
        // waiting is not the same problem as an unusable commit.
        return bufferFormat(surface) ? UpscaleRefusal::EmptyBuffer : UpscaleRefusal::NoBuffer;
    case UpscaleSizing::NotSmaller:
        return UpscaleRefusal::BufferNotSmaller;
    case UpscaleSizing::BelowHalf:
        return UpscaleRefusal::BufferBelowHalf;
    case UpscaleSizing::AspectRatio:
        return UpscaleRefusal::BufferAspectRatio;
    }
    if (surface->bufferTransform() != OutputTransform::Normal) {
        return UpscaleRefusal::TransformedBuffer;
    }
    if (surface->bufferSourceBox() != UpscaleRectF(QPointF(), input)) {
        return UpscaleRefusal::CroppedBuffer;
    }
#if UPSCALE_RENDER_DEVICE_API
    const bool opaque = surface->opaque().contains(surface->rect());
#else
    const bool opaque = surface->opaque().contains(surface->rect().toAlignedRect());
#endif
    if (!opaque) {
        return UpscaleRefusal::TranslucentContent;
    }
    const std::optional<uint32_t> format = bufferFormat(surface);
    if (!format) {
        return UpscaleRefusal::NoBuffer;
    }
    return readableFormat(*format) ? UpscaleRefusal::None : UpscaleRefusal::UnsupportedBufferFormat;
}

UpscaleRefusal windowRefusal(EffectWindow *window)
{
    const UpscaleRefusal placement = placementRefusal(window);
    if (placement != UpscaleRefusal::None) {
        return placement;
    }
    SurfaceItem *surface = window->windowItem()->surfaceItem();
    const UpscaleRefusal shape = surfaceRefusal(window, surface);
    return shape != UpscaleRefusal::None ? shape : contentRefusal(window, surface);
}

UpscaleRefusal passRefusal(const RenderTarget &target, const RenderViewport &viewport, EffectWindow *window,
                           int mask, const WindowPaintData &data)
{
    if (mask & (Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_SCREEN_TRANSFORMED)) {
        return UpscaleRefusal::TransformedPass;
    }
    if (data.opacity() != 1.0) {
        return UpscaleRefusal::TranslucentPass;
    }
    if (data.brightness() != 1.0 || data.saturation() != 1.0) {
        return UpscaleRefusal::AdjustedPass;
    }
    if (!data.toMatrix(viewport.scale()).isIdentity()) {
        return UpscaleRefusal::TransformedPass;
    }
    if (viewport.scale() != window->screen()->scale()) {
        return UpscaleRefusal::ScaledPass;
    }
    // A flipped target is the ordinary case, not an exception: KWin's DRM
    // backend begins every frame with the output's transform combined with
    // OutputTransform::FlipY, so on an upright screen every composed frame
    // arrives flipped. The projection matrix a RenderViewport hands out
    // already carries that transform, and the render tests draw through a
    // flipped target to prove it, so nothing here has to compensate. Refusing
    // it would refuse every frame on real hardware. Orientations beyond the
    // flip stay refused because nothing has drawn through one: a rotated
    // output is already refused by its own condition, above.
    const OutputTransform transform = target.transform();
    if (transform != OutputTransform::Normal && transform != OutputTransform::FlipY) {
        return UpscaleRefusal::TransformedRenderTarget;
    }
    return UpscaleRefusal::None;
}

static QString describeBufferFormat(uint32_t format)
{
    if (format == DRM_FORMAT_INVALID) {
        return i18n("unknown");
    }
    // DRM format codes are four printable characters in little-endian order,
    // such as "XR24". Report the code itself as well, so that a format this
    // build does not know can still be looked up.
    QString code;
    for (int byte = 0; byte < 4; ++byte) {
        const char character = char((format >> (8 * byte)) & 0xFF);
        code.append(character >= ' ' && character <= '~' ? QChar::fromLatin1(character) : QLatin1Char('?'));
    }
    return i18n("%1 (0x%2)", code, QString::number(format, 16));
}

UpscaleBufferKind suppliedBufferKind(SurfaceItem *surface)
{
#if UPSCALE_REGION_API
    GraphicsBuffer *buffer = surface->buffer();
#else
    SurfacePixmap *pixmap = surface->pixmap();
    GraphicsBuffer *buffer = pixmap ? pixmap->buffer() : nullptr;
#endif
    if (!buffer) {
        return UpscaleBufferKind::Unknown;
    }
    if (buffer->dmabufAttributes()) {
        return UpscaleBufferKind::Gpu;
    }
    // A buffer of neither kind is one this KWin exposes through neither
    // accessor, which is not the same as one that came through main memory.
    return buffer->shmAttributes() ? UpscaleBufferKind::SharedMemory : UpscaleBufferKind::Unknown;
}

QString describeSuppliedFormat(SurfaceItem *surface)
{
    const std::optional<uint32_t> format = bufferFormat(surface);
    return format ? describeBufferFormat(*format) : i18n("none");
}

} // namespace KWin
