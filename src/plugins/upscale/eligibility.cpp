/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "eligibility.h"

#include "resolution.h"

#include "effect/effectwindow.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"

#include <KLocalizedString>

#include <drm_fourcc.h>

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

// The window itself: what it is and where it sits, before anything about its
// contents is examined. These checks are cheap and run for every window of
// every frame, so they stay in the order that rejects the common case first.
static UpscaleRefusal placementRefusal(EffectWindow *window)
{
    if (!window->isFullScreen()) {
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
    if (window->opacity() != 1.0) {
        return UpscaleRefusal::TranslucentWindow;
    }
    if (!window->screen()) {
        return UpscaleRefusal::NoOutput;
    }
    if (!window->windowItem() || !window->windowItem()->surfaceItem()) {
        return UpscaleRefusal::NoSurface;
    }
    if (window->screen()->transform() != OutputTransform::Normal) {
        return UpscaleRefusal::TransformedOutput;
    }
    if (window->frameGeometry() != window->screen()->geometryF()) {
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
    if (surface->destinationSize() != window->frameGeometry().size()) {
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

QString describeSuppliedFormat(SurfaceItem *surface)
{
    const std::optional<uint32_t> format = bufferFormat(surface);
    return format ? describeBufferFormat(*format) : i18n("none");
}

QString describeRefusal(UpscaleRefusal refusal)
{
    switch (refusal) {
    case UpscaleRefusal::None:
        return QString();
    case UpscaleRefusal::Disabled:
        return i18n("disabled.");
    case UpscaleRefusal::ResourceFailure:
        return i18n("graphics resource failure; apply settings to retry.");
    case UpscaleRefusal::ScreenLocked:
        return i18n("the screen is locked.");
    case UpscaleRefusal::OtherFullScreenEffect:
        return i18n("another full-screen effect is active.");
    case UpscaleRefusal::SeveralCandidates:
        return i18n("more than one fullscreen window is eligible.");
    case UpscaleRefusal::UnsupportedColors:
        return i18n("this output's colour handling is not supported.");
    case UpscaleRefusal::NoWindow:
        return i18n("there is no window to scale.");
    case UpscaleRefusal::NotFullScreen:
        return i18n("the window is not fullscreen.");
    case UpscaleRefusal::Closing:
        return i18n("the window is closing.");
    case UpscaleRefusal::Minimized:
        return i18n("the window is minimized.");
    case UpscaleRefusal::OtherDesktop:
        return i18n("the window is on another virtual desktop.");
    case UpscaleRefusal::OtherActivity:
        return i18n("the window is on another activity.");
    case UpscaleRefusal::TranslucentWindow:
        return i18n("the window is translucent.");
    case UpscaleRefusal::NoOutput:
        return i18n("the window is not on an output.");
    case UpscaleRefusal::NoSurface:
        return i18n("the window has no surface to capture.");
    case UpscaleRefusal::TransformedOutput:
        return i18n("the output is rotated or flipped.");
    case UpscaleRefusal::NotCoveringOutput:
        return i18n("the window does not exactly cover its output.");
    case UpscaleRefusal::TransformedWindow:
        return i18n("the window is being transformed.");
    case UpscaleRefusal::ChildSurfaces:
        return i18n("the window's surface has child surfaces.");
    case UpscaleRefusal::TransformedSurface:
        return i18n("the window's surface is being transformed.");
    case UpscaleRefusal::OffsetSurface:
        return i18n("the window's surface is offset inside the window.");
    case UpscaleRefusal::TranslucentSurface:
        return i18n("the window's surface is translucent.");
    case UpscaleRefusal::ResizedSurface:
        return i18n("the window's surface is displayed at a different size than the window.");
    case UpscaleRefusal::NoBuffer:
        return i18n("the window has not supplied a buffer yet.");
    case UpscaleRefusal::EmptyBuffer:
        return i18n("the supplied buffer is empty.");
    case UpscaleRefusal::BufferNotSmaller:
        return i18n("the supplied buffer is not smaller than the destination.");
    case UpscaleRefusal::BufferBelowHalf:
        return i18n("the supplied buffer is less than half the destination size.");
    case UpscaleRefusal::BufferAspectRatio:
        return i18n("the supplied buffer has a different aspect ratio than the destination.");
    case UpscaleRefusal::TransformedBuffer:
        return i18n("the supplied buffer is rotated or flipped.");
    case UpscaleRefusal::CroppedBuffer:
        return i18n("only part of the supplied buffer is displayed.");
    case UpscaleRefusal::TranslucentContent:
        return i18n("the supplied buffer is not fully opaque.");
    case UpscaleRefusal::UnsupportedBufferFormat:
        return i18n("the supplied buffer format cannot be read by the scaler.");
    case UpscaleRefusal::TransformedPass:
        return i18n("this frame paints the window or the screen with a transformation.");
    case UpscaleRefusal::TranslucentPass:
        return i18n("this frame paints the window with reduced opacity.");
    case UpscaleRefusal::AdjustedPass:
        return i18n("this frame paints the window with adjusted brightness or saturation.");
    case UpscaleRefusal::ScaledPass:
        return i18n("this frame paints at a different scale than the output.");
    case UpscaleRefusal::TransformedRenderTarget:
        return i18n("this frame's render target has an orientation the scaler does not handle.");
    }
    return QString();
}

} // namespace KWin
