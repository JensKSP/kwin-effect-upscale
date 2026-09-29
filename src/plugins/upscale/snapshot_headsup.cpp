/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The heads-up display's text: the few figures a player watches while playing.

#include "snapshot.h"
#include "snapshot_text.h"

#include <KLocalizedString>

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace KWin
{

// How a resolution is named where people compare them: by its common name
// where it has one, and by its pixels where it does not. "4K" is what a
// television and a graphics setting call 3840 x 2160, and writing it out is
// what makes this line readable at a glance rather than a row of numbers.
static QString resolutionName(const QSize &size)
{
    if (!size.isValid() || size.isEmpty()) {
        return upscaleUnknownText();
    }
    if (size == QSize(3840, 2160)) {
        return i18n("4K");
    }
    if (size == QSize(2560, 1440)) {
        return i18n("1440p");
    }
    if (size == QSize(1920, 1080)) {
        return i18n("1080p");
    }
    if (size == QSize(1280, 720)) {
        return i18n("720p");
    }
    // Equal height does not imply equal resolution, especially on ultrawide
    // outputs. Keep both dimensions when no common name describes this size.
    return upscaleSizeText(size);
}

// What share of the destination the game is actually drawing, in the linear
// per-axis terms every upscaler states its presets in: FSR 1 Quality is
// two thirds, not the four ninths of the pixels that implies.
static QString renderScale(const UpscaleSnapshot &snapshot)
{
    if (snapshot.supplied.width() <= 0 || snapshot.destination.width() <= 0) {
        return QString();
    }
    return i18n("%1%", qRound(100.0 * snapshot.supplied.width() / snapshot.destination.width()));
}

// What the client is, in the fewest words that stay true. It sits on the
// persistent view because that is the first thing to check when a request had
// no effect: a game running through Xwayland cannot be reached by a Wayland
// method, and that is invisible in every other figure there.
//
// "GPU" and "memory" describe how the buffer arrived, not what drew it. The
// graphics API is not observable from a compositor, so it is not claimed.
static QString clientKind(const UpscaleSnapshot &snapshot)
{
    switch (snapshot.windowSystem) {
    case UpscaleWindowSystem::Wayland:
        return i18n("Wayland");
    case UpscaleWindowSystem::X11:
        return i18n("X11");
    case UpscaleWindowSystem::Unknown:
        break;
    }
    return upscaleUnknownText();
}

// One figure, in a field that never changes width.
//
// Four digits and at most one point, always five characters: a value moving
// between 9.999 and 10.00 must not move the text beside it, and on a screen
// the block is read at a glance this matters more than the digits nobody can
// take in anyway. The bounds are the useful ones rather than what a double
// can hold: nothing presents ten thousand frames a second, and a frame slower
// than a second is reported as a second rather than pushing the block wider.
static QString stableNumber(double value)
{
    constexpr double most = 9999;
    constexpr double least = 0.999;
    const double bounded = std::clamp(value, least, most);
    int decimals = 3;
    if (bounded >= 1000) {
        decimals = 0;
    } else if (bounded >= 100) {
        decimals = 1;
    } else if (bounded >= 10) {
        decimals = 2;
    }
    // In the reader's own language: a German session writes 1,053 where a US
    // English one writes 1.053, and QString::number can only write the latter.
    // The width survives it, because a locale that moves the separator does
    // not add one: five columns either way, grouped or not.
    return QLocale().toString(bounded, 'f', decimals).rightJustified(5);
}

QString upscaleHeadsUp(const UpscaleSnapshot &snapshot)
{
    QStringList figures;
    // The rate is the screen's, the frame time is the game's, each taken from
    // the side where it still means something. A screen cannot present more
    // often than it refreshes, so above the refresh the presented rate stops
    // answering what a resolution costs, while the interval between the
    // buffers the client commits keeps answering it.
    // The recent window, not every frame held, so the figure answers for now.
    // It falls back to the whole window for a caller that fills a snapshot
    // without the statistics behind it, such as the settings page.
    const double rate = snapshot.presentedRecent > 0 ? snapshot.presentedRecent : snapshot.presentedRate;
    if (rate > 0) {
        figures.append(i18n("%1 FPS", stableNumber(rate)));
    } else {
        // A dash is what an overlay shows before it has measured anything. It
        // is not a zero, and it is not last minute's rate.
        figures.append(i18n("%1 FPS", QStringLiteral("    —")));
    }
    // "1% low" is the name this figure carries everywhere it is quoted: the
    // mean of the slowest hundredth of the frames, as a rate.
    if (snapshot.presentedLow > 0) {
        figures.append(i18nc("The mean of the slowest hundredth of the frames, as a rate",
                             "1% low %1", stableNumber(snapshot.presentedLow)));
    } else {
        figures.append(i18nc("The mean of the slowest hundredth of the frames, as a rate",
                             "1% low %1", QStringLiteral("    —")));
    }
    if (snapshot.clientUpdates > 0) {
        figures.append(i18nc("Milliseconds per frame the game drew: its frame time",
                             "%1 ms/f", stableNumber(1000.0 / snapshot.clientUpdates)));
    } else {
        figures.append(i18nc("Milliseconds per frame the game drew: its frame time",
                             "%1 ms/f", QStringLiteral("    —")));
    }
    QStringList picture;
    if (snapshot.scaling) {
        QString filter = i18n("FSR 1");
        if (snapshot.filter == UpscaleFilter::Nearest) {
            filter = i18n("Nearest neighbor");
        } else if (snapshot.sharpening > 0) {
            filter = i18n("FSR 1 + RCAS");
        }
        picture.append(filter);
        picture.append(i18n("%1 → %2", upscaleSuppliedForDisplay(snapshot, resolutionName(snapshot.supplied)),
                            resolutionName(snapshot.destination)));
        const QString scale = renderScale(snapshot);
        if (!scale.isEmpty()) {
            picture.append(scale);
        }
    } else if (!snapshot.destination.isEmpty() && snapshot.supplied == snapshot.destination) {
        // Native describes the observed buffer size. Bypassing FSR can still
        // leave KWin enlarging a smaller buffer, so bypass alone is not native.
        picture.append(i18n("%1 native", resolutionName(snapshot.destination)));
    } else if (!snapshot.destination.isEmpty()) {
        picture.append(i18n("FSR off"));
        picture.append(i18n("%1 → %2", upscaleSuppliedForDisplay(snapshot, resolutionName(snapshot.supplied)),
                            resolutionName(snapshot.destination)));
    }
    const QString separator = QStringLiteral("   ");
    const QString first = figures.join(separator);
    QString second = picture.join(separator);
    // What the client is, on the picture line rather than a line of its own:
    // it belongs with what is being drawn, and a block read at a glance
    // mid-game earns no third row for it. It sits at the right edge, where a
    // fact that changes only between games does not push the figures about as
    // the picture line's own text changes length. The block is drawn in a
    // fixed-width font, so a column is a character.
    const QString kind = clientKind(snapshot);
    const qsizetype width = std::max(first.size(), second.size() + separator.size() + kind.size());
    second = second.leftJustified(width - kind.size()) + kind;
    return first + QLatin1Char('\n') + second;
}

} // namespace KWin
