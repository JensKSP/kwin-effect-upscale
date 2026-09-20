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
        position->setToolTip(i18n("Which corner of the game's screen this display occupies. Each display has a "
                                  "corner to itself: choosing one that is taken moves the display that held it "
                                  "to the next free corner."));
    }
    m_osdTimeout->setRange(1, 60);
    m_osdTimeout->setSuffix(i18n(" s"));
    layout->addRow(m_osdDetection);
    layout->addRow(m_osdSummary);
    layout->addRow(i18n("Announcement timeout:"), m_osdTimeout);
    layout->addRow(i18n("Announcement position:"), m_osdAnnouncementPosition);
    m_osdStatistics->setToolTip(i18n("Keeps the frame rate the screen actually presented on screen while a game is "
                                     "running, with the slowest frames beside the average, because an average alone "
                                     "hides stutter."));
    layout->addRow(m_osdStatistics);
    layout->addRow(i18n("Frame rate position:"), m_osdStatisticsPosition);
    layout->addRow(m_osdDeveloper);
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
    connect(m_osdTimeout, &QSpinBox::valueChanged, this, [this]() {
        setNeedsSave(true);
    });
    const std::array<QComboBox *, 3> positions = positionControls();
    for (std::size_t display = 0; display < positions.size(); ++display) {
        connect(positions[display], &QComboBox::currentIndexChanged, this, [this, display]() {
            takeCorner(display);
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
