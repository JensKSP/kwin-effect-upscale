/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Where the on-screen display's blocks go, and how big they are allowed to be.
//
// Separate from display_test.cpp for two reasons. That file had outgrown the
// file-size limit, and these two cases are the ones in it that never needed a
// GPU: they read the rectangles placement produces and the sizes the overlay
// lays text out to, neither of which is painted. Keeping them apart means the
// rules that stop two blocks meeting are checked anywhere the tests build,
// with no OpenGL context, no software rasteriser and no offscreen platform.

#include "overlay.h"
#include "placement.h"
#include "snapshot.h"

#include <QGuiApplication>
#include <QRectF>
#include <QTest>

#include <array>
#include <cstddef>

using namespace KWin;

class UpscalePlacementTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cornersAreKeptApart();
    void blocksStayInTheirQuarter();
};

// Choosing a corner someone else holds moves them on, and a file that names
// one corner twice is separated again. With three displays and four corners
// there is always somewhere for the displaced one to go.
void UpscalePlacementTest::cornersAreKeptApart()
{
    const auto distinct = [](const std::array<UpscaleCorner, 3> &corners) {
        return corners[0] != corners[1] && corners[1] != corners[2] && corners[0] != corners[2];
    };

    // The announcement takes the corner the heads-up held. The heads-up steps
    // to the next free corner rather than swapping into the one just vacated.
    std::array<UpscaleCorner, 3> corners{UpscaleCorner::TopLeft, UpscaleCorner::TopRight, UpscaleCorner::BottomRight};
    upscaleTakeCorner(corners, 0, UpscaleCorner::TopRight);
    QCOMPARE(corners[0], UpscaleCorner::TopRight);
    QCOMPARE(corners[1], UpscaleCorner::BottomLeft);
    QCOMPARE(corners[2], UpscaleCorner::BottomRight);
    QVERIFY(distinct(corners));

    // The next free corner wraps past the end of the list.
    corners = {UpscaleCorner::TopLeft, UpscaleCorner::TopRight, UpscaleCorner::BottomRight};
    upscaleTakeCorner(corners, 1, UpscaleCorner::BottomRight);
    QCOMPARE(corners[1], UpscaleCorner::BottomRight);
    QCOMPARE(corners[2], UpscaleCorner::TopRight);
    QVERIFY(distinct(corners));

    // Choosing the corner a display already holds moves nothing.
    corners = {UpscaleCorner::TopLeft, UpscaleCorner::TopRight, UpscaleCorner::BottomRight};
    const std::array<UpscaleCorner, 3> before = corners;
    upscaleTakeCorner(corners, 2, UpscaleCorner::BottomRight);
    QCOMPARE(corners, before);

    // Nothing in the settings can write a duplicate, but a hand-edited file
    // can, and the worst case is all three naming one corner.
    corners = {UpscaleCorner::BottomLeft, UpscaleCorner::BottomLeft, UpscaleCorner::BottomLeft};
    upscaleSeparateCorners(corners);
    QCOMPARE(corners[0], UpscaleCorner::BottomLeft);
    QVERIFY2(distinct(corners), "three displays on one corner were not separated");

    // Separating keeps the earlier entry where it was, so a duplicate moves
    // the display that was listed later and never the one before it.
    corners = {UpscaleCorner::TopRight, UpscaleCorner::TopRight, UpscaleCorner::TopLeft};
    upscaleSeparateCorners(corners);
    QCOMPARE(corners[0], UpscaleCorner::TopRight);
    QCOMPARE(corners[2], UpscaleCorner::TopLeft);
    QVERIFY(distinct(corners));

    // Already distinct corners are left exactly as they are.
    corners = {UpscaleCorner::BottomRight, UpscaleCorner::TopLeft, UpscaleCorner::BottomLeft};
    const std::array<UpscaleCorner, 3> untouched = corners;
    upscaleSeparateCorners(corners);
    QCOMPARE(corners, untouched);
}

// The defect this bound exists for, at the geometry it was reported at.
//
// A television at 3840 x 2160 configured at scale 3 is 1280 x 720 in the
// coordinates placement works in, while the blocks are drawn at three times
// the pixels and therefore keep their size in those coordinates. A block that
// took a quarter of an unscaled screen wants three quarters of this one, and
// before it was bounded the announcement and the heads-up overlapped.
//
// This works on the placed rectangles rather than on a painted frame: at this
// scale a faithful readback would be a 3840 x 2160 floating-point image, and
// what is being asserted is geometry, which the rectangles state exactly.
void UpscalePlacementTest::blocksStayInTheirQuarter()
{
    UpscaleSnapshot state;
    state.window = QStringLiteral("Tux Racer");
    state.output = QStringLiteral("HDMI-A-1");
    state.application = QStringLiteral("supertuxkart");
    state.selected = true;
    state.scaling = true;
    state.supplied = QSize(1920, 1080);
    state.destination = QSize(3840, 2160);
    state.presentedRecent = 59.94;
    state.presentedLow = 41.5;
    state.clientUpdates = 120;

    const QSizeF screen(1280, 720);
    constexpr double margin = 32;
    // The corners the three displays hold by default, in the order they are
    // stored. The heads-up is the one carrying the emphasis.
    const std::array<UpscaleCorner, 3> corners{UpscaleCorner::TopLeft, UpscaleCorner::TopRight, UpscaleCorner::BottomRight};
    const std::array<double, 3> emphasis{1, 1.6, 1};
    const std::array<QString, 3> texts{
        upscaleAnnouncement(state) + QLatin1Char('\n') + upscaleBasicSummary(state),
        upscaleHeadsUp(state),
        upscaleDeveloperInformation(state),
    };

    const auto quarter = [&screen](UpscaleCorner corner) {
        const bool atTop = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::TopRight;
        const bool atLeft = corner == UpscaleCorner::TopLeft || corner == UpscaleCorner::BottomLeft;
        return QRectF(atLeft ? 0 : screen.width() / 2, atTop ? 0 : screen.height() / 2,
                      screen.width() / 2, screen.height() / 2);
    };

    // Every scale factor KDE offers for a television of this size, and the
    // unscaled case that used to be the only one that fitted.
    for (const double scale : {1.0, 1.5, 2.0, 3.0}) {
        UpscaleCornerLayout layout(QPointF(), screen, margin);
        std::array<QRectF, 3> placed;
        for (std::size_t block = 0; block < placed.size(); ++block) {
            UpscaleOverlay overlay;
            overlay.setText(texts[block], scale, emphasis[block]);
            overlay.fit(layout.budget(corners[block]));
            const QSizeF size = overlay.size();
            QVERIFY2(!size.isEmpty(), qPrintable(QStringLiteral("block %1 vanished at scale %2").arg(block).arg(scale)));
            placed[block] = QRectF(layout.place(corners[block], size), size);
        }
        for (std::size_t block = 0; block < placed.size(); ++block) {
            // Half a pixel of slack: the rectangles are laid out in whole
            // destination pixels and divided by the scale to get here.
            const QRectF own = quarter(corners[block]).adjusted(-0.5, -0.5, 0.5, 0.5);
            QVERIFY2(own.contains(placed[block]),
                     qPrintable(QStringLiteral("block %1 left its quarter at scale %2: %3 x %4 at %5, %6")
                                    .arg(block)
                                    .arg(scale)
                                    .arg(placed[block].width())
                                    .arg(placed[block].height())
                                    .arg(placed[block].x())
                                    .arg(placed[block].y())));
        }
        for (std::size_t block = 0; block < placed.size(); ++block) {
            for (std::size_t other = block + 1; other < placed.size(); ++other) {
                QVERIFY2(!placed[block].intersects(placed[other]),
                         qPrintable(QStringLiteral("blocks %1 and %2 overlapped at scale %3")
                                        .arg(block)
                                        .arg(other)
                                        .arg(scale)));
            }
        }
    }

    // The bound is a bound and not a preference: text that cannot be laid out
    // small enough to fit is cropped rather than allowed past the centre line.
    UpscaleCornerLayout cramped(QPointF(), QSizeF(320, 200), margin);
    UpscaleOverlay overlay;
    overlay.setText(texts[2], 1, 1);
    const QSizeF budget = cramped.budget(UpscaleCorner::BottomRight);
    overlay.fit(budget);
    QVERIFY(!overlay.size().isEmpty());
    QVERIFY2(overlay.size().width() <= budget.width() && overlay.size().height() <= budget.height(),
             "a block that could not be shrunk to fit was not cropped to its quarter");

    // No room is its own answer: nothing is drawn. A crop to a rectangle of no
    // size is a null rectangle, which QImage::copy() reads as "copy it all",
    // so without a case of its own this drew the whole block into a corner
    // that had none to give.
    UpscaleOverlay nowhere;
    nowhere.setText(texts[2], 1, 1);
    nowhere.fit(QSizeF(0, 0));
    QVERIFY2(nowhere.size().isEmpty(), "a block given no room was drawn anyway");
}

QTEST_MAIN(UpscalePlacementTest)

#include "placement_test.moc"
