/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resolutionchoice.h"

#include "resolution.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace KWin
{

// The separator this project writes between the two numbers everywhere else it
// shows a size, so the control reads like the rest of the effect.
static QString resolutionLabel(const QSize &size)
{
    // As text, not as integers: a locale groups integers, and "1.920 × 1.080"
    // reads as two fractional numbers rather than as a resolution. The rest of
    // this effect writes sizes the same way, for the same reason.
    return i18nc("A screen resolution: width by height", "%1 × %2",
                 QString::number(size.width()), QString::number(size.height()));
}

// What a person types is not what a program prints. An ASCII x is on every
// keyboard and the multiplication sign is not, so both are read, with or
// without the spaces around them.
static QSize parseResolution(const QString &text)
{
    static const QRegularExpression s_pattern(QStringLiteral("^\\s*(\\d{1,6})\\s*[x×X]\\s*(\\d{1,6})\\s*$"));
    const QRegularExpressionMatch match = s_pattern.match(text);
    if (!match.hasMatch()) {
        return {};
    }
    return {match.captured(1).toInt(), match.captured(2).toInt()};
}

// The sizes the list offers, each with the name of the screen showing it where
// one does. The screens this system is showing right now come first, because
// a threshold is nearly always about one of them. A screen reports its size
// in layout pixels, which a scaled desktop shrinks; the threshold is about
// physical pixels, so the scale factor goes back in. Then the sizes people
// mean anyway, so a threshold can be set for a television that is not plugged
// in while the settings are open.
static QList<std::pair<QSize, QString>> offeredResolutions()
{
    QList<std::pair<QSize, QString>> offered;
    const auto isOffered = [&offered](const QSize &size) {
        return std::ranges::any_of(offered, [&size](const auto &entry) {
            return entry.first == size;
        });
    };
    for (const UpscaleScreen &screen : upscaleScreens()) {
        if (!screen.pixels.isEmpty() && !isOffered(screen.pixels)) {
            offered.append({screen.pixels, screen.name});
        }
    }
    for (const QSize &size : {QSize(3840, 2160), QSize(2560, 1440), QSize(1920, 1080), QSize(1280, 720)}) {
        if (!isOffered(size)) {
            offered.append({size, QString()});
        }
    }
    return offered;
}

// Counted wide enough for any size a person can type, and clamped to what the
// threshold can store rather than wrapping round to a small or negative one.
static int pixelCount(const QSize &size)
{
    return int(std::min<qint64>(qint64(size.width()) * size.height(), std::numeric_limits<int>::max()));
}

void upscaleFillResolutions(QComboBox *box)
{
    const QSignalBlocker blocker(box);
    const int selected = upscaleResolutionPixels(box, 0);
    box->clear();
    box->addItem(i18n("Any screen"), 0);
    for (const auto &[size, screen] : offeredResolutions()) {
        box->addItem(screen.isEmpty() ? resolutionLabel(size)
                                      : i18nc("A resolution belonging to a connected screen", "%1 (%2)", resolutionLabel(size), screen),
                     pixelCount(size));
    }
    upscaleSelectResolution(box, selected);
}

QString upscaleResolutionName(int pixels)
{
    if (pixels <= 0) {
        return i18n("Any screen");
    }
    for (const auto &entry : offeredResolutions()) {
        if (pixelCount(entry.first) == pixels) {
            return resolutionLabel(entry.first);
        }
    }
    return i18nc("A threshold that matches no resolution", "%1 pixels", QString::number(pixels));
}

QList<UpscaleScreen> upscaleScreens()
{
    QList<UpscaleScreen> screens;
    for (const QScreen *screen : QGuiApplication::screens()) {
        screens.append({screen->geometry().size() * screen->devicePixelRatio(), screen->name()});
    }
    return screens;
}

UpscaleScreen upscaleLargestScreen()
{
    const QList<UpscaleScreen> screens = upscaleScreens();
    const auto largest = std::ranges::max_element(screens, {}, [](const UpscaleScreen &screen) {
        return qint64(screen.pixels.width()) * screen.pixels.height();
    });
    return largest != screens.end() ? *largest : UpscaleScreen{};
}

QList<int> upscaleSnapScales(const QSize &screen)
{
    // The sizes monitors, televisions and games offer, in the shapes screens
    // come in: 16:9, 16:10, the two ultrawides, 32:9 and 4:3.
    static constexpr std::array<std::pair<int, int>, 29> s_known{{
        {1280, 720},
        {1600, 900},
        {1920, 1080},
        {2048, 1152},
        {2560, 1440},
        {2880, 1620},
        {3200, 1800},
        {3840, 2160},
        {5120, 2880},
        {7680, 4320},
        {1280, 800},
        {1440, 900},
        {1680, 1050},
        {1920, 1200},
        {2560, 1600},
        {2880, 1800},
        {3840, 2400},
        {2560, 1080},
        {3440, 1440},
        {3840, 1600},
        {5120, 2160},
        {3840, 1080},
        {5120, 1440},
        {1024, 768},
        {1280, 960},
        {1600, 1200},
        {2048, 1536},
        {1366, 768},
        {1600, 1024},
    }};
    QList<int> scales;
    if (screen.isEmpty()) {
        return scales;
    }
    for (const auto &[width, height] : s_known) {
        if (width > screen.width() || width * 2 < screen.width()) {
            continue;
        }
        // The nearest scale in basis points, and its neighbours, because the
        // rounding of either side can land one pixel off.
        const int nearest = qRound(10000.0 * width / screen.width());
        for (const int scale : {nearest, nearest - 1, nearest + 1}) {
            const UpscaleSize size = desiredResolution({screen.width(), screen.height()}, ResolutionPreset::Custom, scale);
            if (size.width == width && size.height == height && scale >= 5000 && scale <= 10000) {
                scales.append(scale);
                break;
            }
        }
    }
    std::ranges::sort(scales);
    return scales;
}

void upscaleSelectResolution(QComboBox *box, int pixels)
{
    const int known = box->findData(pixels);
    if (known >= 0) {
        box->setCurrentIndex(known);
        return;
    }
    // A count that matches no resolution was set by hand or by an older
    // version. It is shown as what it is rather than rounded to a resolution
    // nobody chose, and typing a resolution over it replaces it.
    box->setCurrentIndex(-1);
    box->setEditText(i18nc("A threshold that matches no resolution", "%1 pixels", QString::number(pixels)));
}

int upscaleResolutionPixels(const QComboBox *box, int fallback)
{
    const int current = box->currentIndex();
    if (current >= 0 && box->itemText(current) == box->currentText()) {
        return box->itemData(current).toInt();
    }
    const QSize typed = parseResolution(box->currentText());
    if (!typed.isEmpty()) {
        return pixelCount(typed);
    }
    // Whatever else the field holds, including the pixel count shown for a
    // threshold no resolution matches.
    static const QRegularExpression s_count(QStringLiteral("^\\s*(\\d{1,9})"));
    const QRegularExpressionMatch match = s_count.match(box->currentText());
    return match.hasMatch() ? match.captured(1).toInt() : fallback;
}

}
