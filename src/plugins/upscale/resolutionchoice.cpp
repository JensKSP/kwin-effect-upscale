/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resolutionchoice.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>

#include <algorithm>

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

void upscaleFillResolutions(QComboBox *box)
{
    const QSignalBlocker blocker(box);
    const int selected = upscaleResolutionPixels(box, 0);
    box->clear();
    box->addItem(i18n("Every output (no threshold)"), 0);
    // The screens this system is showing right now come first and carry their
    // names, because a threshold is nearly always about one of them. A screen
    // reports its size in layout pixels, which a scaled desktop shrinks; the
    // threshold is about physical pixels, so the scale factor goes back in.
    QList<QSize> offered;
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QSize pixels = screen->geometry().size() * screen->devicePixelRatio();
        if (pixels.isEmpty() || offered.contains(pixels)) {
            continue;
        }
        offered.append(pixels);
        box->addItem(i18nc("A resolution belonging to a connected screen", "%1 (%2)",
                           resolutionLabel(pixels), screen->name()),
                     pixels.width() * pixels.height());
    }
    // Then the sizes people mean anyway, so a threshold can be set for a
    // television that is not plugged in while the settings are open.
    for (const QSize &size : {QSize(3840, 2160), QSize(2560, 1440), QSize(1920, 1080), QSize(1280, 720)}) {
        if (!offered.contains(size)) {
            box->addItem(resolutionLabel(size), size.width() * size.height());
        }
    }
    upscaleSelectResolution(box, selected);
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
        return typed.width() * typed.height();
    }
    // Whatever else the field holds, including the pixel count shown for a
    // threshold no resolution matches.
    static const QRegularExpression s_count(QStringLiteral("^\\s*(\\d{1,9})"));
    const QRegularExpressionMatch match = s_count.match(box->currentText());
    return match.hasMatch() ? match.captured(1).toInt() : fallback;
}

}
