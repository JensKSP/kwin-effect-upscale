/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "resolutionpreview.h"

#include "resolutionchoice.h"

#include <KLocalizedString>

#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>

namespace KWin
{

UpscaleResolutionPreview::UpscaleResolutionPreview(QObject *parent)
    : QObject(parent)
{
}

void UpscaleResolutionPreview::build(QFormLayout *form, QWidget *parent, const QString &name)
{
    m_label = new QLabel(parent);
    m_label->setObjectName(name);
    // One screen per line, each short enough never to need wrapping. A
    // word-wrapped label in a form is laid out at a narrow width and would
    // break every one of them.
    m_label->setTextFormat(Qt::PlainText);
    form->addRow(QString(), m_label);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &UpscaleResolutionPreview::watchScreens);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &UpscaleResolutionPreview::watchScreens);
    watchScreens();
}

void UpscaleResolutionPreview::show(ResolutionPreset preset, int basisPoints, int minimumPixels)
{
    m_preset = preset;
    m_basisPoints = basisPoints;
    m_minimumPixels = minimumPixels;
    update();
}

// Screens come and go, and change mode or scale, while the page is open.
void UpscaleResolutionPreview::watchScreens()
{
    for (QScreen *screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, &UpscaleResolutionPreview::update, Qt::UniqueConnection);
        connect(screen, &QScreen::physicalDotsPerInchChanged, this, &UpscaleResolutionPreview::update, Qt::UniqueConnection);
    }
    update();
}

void UpscaleResolutionPreview::update()
{
    if (m_preset == ResolutionPreset::Native) {
        m_label->setText(i18n("Renders at full resolution. Nothing is upscaled."));
        return;
    }
    // Physical pixels, which is what the effect compares and scales. The
    // limit decides before the preset does: a screen at or below it is left
    // alone whatever the preset would have asked for.
    QStringList lines;
    for (const UpscaleScreen &screen : upscaleScreens()) {
        const UpscaleSize output{screen.pixels.width(), screen.pixels.height()};
        const QString size = i18nc("A screen resolution: width by height", "%1 × %2", QString::number(output.width),
                                   QString::number(output.height));
        // A screen without a name, as a virtual one can be, is named by its size.
        const QString name = screen.name.isEmpty() ? size : i18nc("A screen's name, then its resolution", "%1 (%2)", screen.name, size);
        if (!exceedsMinimumPixels(output, m_minimumPixels)) {
            lines.append(i18nc("%1 is a screen", "%1: not upscaled, at or below the limit", name));
            continue;
        }
        const UpscaleSize desired = desiredResolution(output, m_preset, m_basisPoints);
        lines.append(i18nc("%1 is a screen, %2 × %3 the render resolution", "%1: renders at %2 × %3", name,
                           QString::number(desired.width), QString::number(desired.height)));
    }
    m_label->setText(lines.join(QLatin1Char('\n')));
}

} // namespace KWin
