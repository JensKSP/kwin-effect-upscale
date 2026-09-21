/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The settings page's on-screen display section: which of the four displays
// are shown, how long an announcement lasts, and which corner each one
// occupies. It is a separate translation unit from the rest of the page
// because the page as a whole had outgrown the file-size limit, and this is
// the part of it that stands on its own: everything here is about the blocks
// drawn over the game, and nothing else on the page needs any of it.

#include "upscale_config.h"

#include "placement.h"
#include "upscaleconfig.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QSpinBox>

namespace KWin
{

void UpscaleEffectConfig::addDisplayControls(QFormLayout *layout)
{
    m_osdDetection->setObjectName(QStringLiteral("osdDetection"));
    m_osdSummary->setObjectName(QStringLiteral("osdSummary"));
    m_osdStatistics->setObjectName(QStringLiteral("osdStatistics"));
    m_osdDeveloper->setObjectName(QStringLiteral("osdDeveloper"));
    m_osdAnnouncementPosition->setObjectName(QStringLiteral("osdAnnouncementPosition"));
    m_osdStatisticsPosition->setObjectName(QStringLiteral("osdStatisticsPosition"));
    m_osdDeveloperPosition->setObjectName(QStringLiteral("osdDeveloperPosition"));
    m_osdTimeout->setObjectName(QStringLiteral("osdTimeout"));
    // The order is the stored one in upscaleconfig.kcfg.
    for (QComboBox *position : positionControls()) {
        position->addItems({i18n("Top left"), i18n("Top right"), i18n("Bottom left"), i18n("Bottom right")});
        position->setToolTip(i18n("Each display uses its own corner. Choosing a corner in use moves the other "
                                  "display to the next free one."));
    }
    m_osdTimeout->setRange(1, 60);
    // The unit is written out and follows the number, the way KWin's own
    // effect pages write theirs, so the suffix is set again as the value moves.
    const auto unit = [this](int seconds) {
        m_osdTimeout->setSuffix(i18ncp("Suffix", " second", " seconds", seconds));
    };
    unit(m_osdTimeout->value());
    // Three displays, each a switch and what it decides, set a little apart:
    // the switches sit in the field column like every other value, and the
    // space says where one display ends and the next begins.
    const auto gap = [this, layout]() {
        layout->addItem(new QSpacerItem(0, widget()->fontMetrics().height() / 2, QSizePolicy::Minimum, QSizePolicy::Fixed));
    };
    layout->addRow(QString(), m_osdDetection);
    layout->addRow(QString(), m_osdSummary);
    layout->addRow(i18n("Show startup info for:"), m_osdTimeout);
    layout->addRow(i18n("Startup info position:"), m_osdAnnouncementPosition);
    gap();
    m_osdStatistics->setToolTip(i18n("Shows the average frame rate and the slowest frames while a game is running."));
    layout->addRow(QString(), m_osdStatistics);
    layout->addRow(i18n("Frame rate position:"), m_osdStatisticsPosition);
    gap();
    layout->addRow(QString(), m_osdDeveloper);
    layout->addRow(i18n("Developer information position:"), m_osdDeveloperPosition);
    // A Debug build shows statistics and developer information unless the
    // user has said otherwise; a release build shows only the announcement.
    // The defaults live in upscaleconfig.kcfg, not here.
    for (QCheckBox *box : {m_osdDetection, m_osdSummary, m_osdStatistics, m_osdDeveloper}) {
        connect(box, &QCheckBox::toggled, this, [this]() {
            updatePreview();
            setNeedsSave(true);
        });
    }
    connect(m_osdTimeout, &QSpinBox::valueChanged, this, [this, unit](int seconds) {
        unit(seconds);
        updatePreview();
        setNeedsSave(true);
    });
    const std::array<QComboBox *, 3> positions = positionControls();
    for (std::size_t display = 0; display < positions.size(); ++display) {
        connect(positions[display], &QComboBox::currentIndexChanged, this, [this, display]() {
            takeCorner(display);
            updatePreview();
            setNeedsSave(true);
        });
    }
}

std::array<QComboBox *, 3> UpscaleEffectConfig::positionControls()
{
    // The order the three corners are stored and separated in.
    return {m_osdAnnouncementPosition, m_osdStatisticsPosition, m_osdDeveloperPosition};
}

// Choosing a corner another display holds moves that display rather than
// refusing the choice or letting the two sit on top of each other. The move
// happens while the person is looking at the page, so they see where the
// other display went instead of discovering it the next time they play.
void UpscaleEffectConfig::takeCorner(std::size_t display)
{
    const std::array<QComboBox *, 3> positions = positionControls();
    std::array<UpscaleCorner, 3> corners{};
    for (std::size_t entry = 0; entry < positions.size(); ++entry) {
        corners[entry] = upscaleCorner(positions[entry]->currentIndex());
    }
    upscaleTakeCorner(corners, display, corners[display]);
    for (std::size_t entry = 0; entry < positions.size(); ++entry) {
        // Writing a box back would run this again through its own signal, and
        // the second pass would move a display that had already stepped aside.
        const QSignalBlocker blocker(positions[entry]);
        positions[entry]->setCurrentIndex(int(corners[entry]));
    }
}

} // namespace KWin
