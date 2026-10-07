/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What a window drawn over its output does with what its picture covers - the
// pointer's cursor over a decoration the picture hides - and a window whose
// picture is in a subsurface of its own, as Wine's Wayland driver draws one.

#include "kwin_scaling_test.h"

#include "core/inputdevice.h"
#include "input.h"
#include "pointer_input.h"
#include "wayland/seat.h"
#include "wayland_server.h"
#include "window.h"

#include <KWayland/Client/output.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/subsurface.h>
#include <KWayland/Client/surface.h>

#include <QColor>

// A decorated window under a game drawn over its output, whose decoration the
// picture hides. KWin finds that decoration under the pointer before any
// filter runs, and while a decoration has the pointer KWin shows that
// decoration's cursor (CursorImage::reevaluteSource()): an arrow over the title
// bar, a resize cursor at the border, where the player sees the game. Without
// one it shows the cursor of the surface the seat's pointer is on.
void UpscaleProductionTest::aHiddenDecorationKeepsTheGamesCursor()
{
    quint32 time = 0;
    Test::pointerMotion(QPointF(382, 214), ++time);
    std::unique_ptr<KWayland::Client::Surface> belowSurface = Test::createSurface();
    std::unique_ptr<Test::XdgToplevel> belowShell = Test::createXdgToplevelSurface(belowSurface.get(), Test::CreationSetup::CreateOnly);
    std::unique_ptr<Test::XdgToplevelDecorationV1> decoration = Test::createXdgToplevelDecorationV1(belowShell.get());
    QSignalSpy configured(belowShell->xdgSurface(), &Test::XdgSurface::configureRequested);
    decoration->set_mode(Test::XdgToplevelDecorationV1::mode_server_side);
    belowSurface->commit(KWayland::Client::Surface::CommitFlag::None);
    QVERIFY(configured.wait());
    belowShell->xdgSurface()->ack_configure(configured.last().at(0).value<quint32>());
    Window *below = Test::renderAndWaitForShown(belowSurface.get(), QSize(160, 60), Qt::blue);
    QVERIFY(below);
    QVERIFY(below->isDecorated());
    below->move(QPointF(8, 8));

    // The game: a plain window of the size it was told at connection, which
    // the effect draws over the whole output and stacks above the other.
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    std::unique_ptr<KWayland::Client::Surface> gameSurface = Test::createSurface();
    std::unique_ptr<Test::XdgToplevel> gameShell = Test::createXdgToplevelSurface(gameSurface.get());
    Window *game = Test::renderAndWaitForShown(gameSurface.get(), pattern(QSize(256, 144)));
    QVERIFY(game);
    game->move(QPointF(120, 64));
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    Test::pointerMotion(game->frameGeometry().center(), ++time);
    QTRY_COMPARE(waylandServer()->seat()->focusedPointerSurface(), game->surface());

    // Over the hidden title bar, and over the hidden corner where KWin would
    // offer a resize: the picture is the game's, and so is the cursor.
    const QRectF frame = below->frameGeometry();
    for (const QPointF &hidden : {QPointF(frame.left() + 40, frame.top() + 4), frame.topLeft() + QPointF(1, 1)}) {
        QVERIFY2(!game->frameGeometry().contains(hidden), "the point has to lie beside the game, over the other window");
        Test::pointerMotion(hidden, ++time);
        QCOMPARE(input()->pointer()->hover(), below);
        QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), game->surface());
        QVERIFY2(!input()->pointer()->decoration(),
                 qPrintable(QStringLiteral("at %1, %2 the hidden decoration has the pointer, and KWin shows its cursor").arg(hidden.x()).arg(hidden.y())));
    }
}

// Wine's Wayland driver draws a game into a subsurface covering its window,
// whose own buffer shows nothing of it, and rounds the subsurface's buffer up
// to a height that is a multiple of 128, showing the told size of it through a
// viewport. The picture is the subsurface's, as much of it as the viewport
// shows: 256 × 144 here, of a 256 × 160 buffer, in a window of the told size.
void UpscaleProductionTest::presentsThePictureOfItsOnlySubsurface_data()
{
    QTest::addColumn<bool>("alpha");
    QTest::addColumn<QColor>("below");
    QTest::newRow("opaque") << false << QColor(Qt::black);
    // As Wine 10.0's driver draws an OpenGL game: a buffer with an alpha
    // channel and no opaque region, over a window buffer left transparent.
    QTest::newRow("alpha") << true << QColor(Qt::transparent);
    // A picture clear on its left half lets the window's own buffer through
    // there, as KWin composites the two.
    QTest::newRow("translucent") << true << QColor(Qt::green);
}

void UpscaleProductionTest::presentsThePictureOfItsOnlySubsurface()
{
    QFETCH(bool, alpha);
    QFETCH(QColor, below);
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    std::unique_ptr<KWayland::Client::Surface> surface = Test::createSurface();
    std::unique_ptr<KWayland::Client::Surface> content = Test::createSurface();
    std::unique_ptr<KWayland::Client::SubSurface> subsurface = Test::createSubSurface(content.get(), surface.get());
    QVERIFY(subsurface);
    Viewport viewport(m_viewporter.get_viewport(*content));
    viewport.set_source(wl_fixed_from_int(0), wl_fixed_from_int(0), wl_fixed_from_int(256), wl_fixed_from_int(144));
    viewport.set_destination(256, 144);
    QImage picture = pattern(QSize(256, 160)).convertToFormat(alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    const bool through = alpha && below.alpha() == 255;
    if (through) {
        for (int y = 0; y < picture.height(); ++y) {
            for (int x = 0; x < picture.width() / 2; ++x) {
                picture.setPixel(x, y, 0);
            }
        }
    }
    Test::render(content.get(), picture);
    std::unique_ptr<Test::XdgToplevel> shell = Test::createXdgToplevelSurface(surface.get());
    QImage own(QSize(256, 144), below.alpha() == 255 ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
    own.fill(below);
    Window *window = Test::renderAndWaitForShown(surface.get(), own);
    QVERIFY(window);
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("supplied=256x144")), qPrintable(status()));
    // The picture is the subsurface's, not the window's own buffer: the
    // pattern is blue where it lies right of its diagonal, as in the upper
    // right quarter, away from the middle where KWin draws the cursor of a
    // pointer nothing has moved yet.
    const QImage output = renderOutput();
    QVERIFY(!output.isNull());
    const QPoint blue(output.width() * 3 / 4, output.height() / 4);
    QVERIFY2(qBlue(output.pixel(blue)) > 100, qPrintable(QString::number(output.pixel(blue), 16)));
    if (through) {
        const QPoint green(output.width() / 8, output.height() / 2);
        QVERIFY2(qGreen(output.pixel(green)) > 200 && qBlue(output.pixel(green)) < 50,
                 qPrintable(QString::number(output.pixel(green), 16)));
    }
}

namespace
{
// A pen as a client of the tablet protocol sees it: the surface it is near,
// where on it, and whether its tip is down.
class TestPen : public QtWayland::zwp_tablet_tool_v2
{
public:
    explicit TestPen(::zwp_tablet_tool_v2 *pen)
        : QtWayland::zwp_tablet_tool_v2(pen)
    {
    }
    ~TestPen() override
    {
        destroy();
    }
    wl_surface *surface = nullptr;
    QPointF position;
    bool down = false;

protected:
    void zwp_tablet_tool_v2_proximity_in(uint32_t, ::zwp_tablet_v2 *, ::wl_surface *entered) override
    {
        surface = entered;
    }
    void zwp_tablet_tool_v2_proximity_out() override
    {
        surface = nullptr;
    }
    void zwp_tablet_tool_v2_motion(wl_fixed_t x, wl_fixed_t y) override
    {
        position = QPointF(wl_fixed_to_double(x), wl_fixed_to_double(y));
    }
    void zwp_tablet_tool_v2_down(uint32_t) override
    {
        down = true;
    }
    void zwp_tablet_tool_v2_up() override
    {
        down = false;
    }
};

class TestTablet : public QtWayland::zwp_tablet_v2
{
public:
    explicit TestTablet(::zwp_tablet_v2 *tablet)
        : QtWayland::zwp_tablet_v2(tablet)
    {
    }
    ~TestTablet() override
    {
        destroy();
    }
};

class TestTabletSeat : public QtWayland::zwp_tablet_seat_v2
{
public:
    explicit TestTabletSeat(::zwp_tablet_seat_v2 *seat)
        : QtWayland::zwp_tablet_seat_v2(seat)
    {
    }
    ~TestTabletSeat() override
    {
        pens.clear();
        tablets.clear();
        destroy();
    }
    std::vector<std::unique_ptr<TestTablet>> tablets;
    std::vector<std::unique_ptr<TestPen>> pens;

protected:
    void zwp_tablet_seat_v2_tablet_added(::zwp_tablet_v2 *tablet) override
    {
        tablets.push_back(std::make_unique<TestTablet>(tablet));
    }
    void zwp_tablet_seat_v2_tool_added(::zwp_tablet_tool_v2 *pen) override
    {
        pens.push_back(std::make_unique<TestPen>(pen));
    }
};
}

// A pen over a game drawn over its output reaches the game where the picture
// shows it, as the pointer and a touch do. KWin hands it to the window under
// it at the window's own place, which is not where the picture shows it.
void UpscaleProductionTest::mapsAPenOntoThePicture()
{
    QVERIFY(m_tablets.isInitialized());
    TestTabletSeat seat(m_tablets.get_tablet_seat(*Test::waylandSeat()));
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    std::unique_ptr<KWayland::Client::Surface> surface = Test::createSurface();
    std::unique_ptr<Test::XdgToplevel> shell = Test::createXdgToplevelSurface(surface.get());
    Window *game = Test::renderAndWaitForShown(surface.get(), pattern(QSize(256, 144)));
    QVERIFY(game);
    game->move(QPointF(120, 64));
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    // The output's middle shows the window's: 192, 108 of 384 x 216 is 128, 72
    // of 256 x 144. KWin would hand the pen 72, 44, from the window's place.
    // KWin tells clients of a pen with its first event, before any filter.
    // The tablet's objects are on this test's own event queue: a roundtrip
    // reads what the compositor sent, and the queue is then dispatched here.
    const auto heard = [this]() {
        Test::waylandSync();
        m_queue->dispatch();
        return true;
    };
    quint32 time = 0;
    Test::tabletToolProximityEvent(QPointF(192, 108), 0, 0, 0, 0, 0, false, true, ++time);
    QTRY_VERIFY(heard() && !seat.pens.empty());
    TestPen *pen = seat.pens.front().get();
    QTRY_VERIFY(heard() && pen->surface == static_cast<wl_surface *>(*surface));
    QTRY_COMPARE((heard(), pen->position), QPointF(128, 72));
    auto *application = static_cast<WaylandTestApplication *>(kwinApp());
    Q_EMIT application->virtualTablet()->tabletToolTipEvent(QPointF(96, 54), 1, 0, 0, 0, 0, true, true, application->virtualTabletTool(),
                                                            std::chrono::milliseconds(++time), application->virtualTablet());
    QTRY_VERIFY(heard() && pen->down);
    QTRY_COMPARE((heard(), pen->position), QPointF(64, 36));
}

// A pen that comes near in a bar is told of once it reaches the picture, and
// one pressed on the picture and lifted in a bar is lifted, as the tablet
// protocol asks: proximity before motion, and an end to every contact.
void UpscaleProductionTest::keepsAPenWholeAcrossABar()
{
    QVERIFY(m_tablets.isInitialized());
    TestTabletSeat seat(m_tablets.get_tablet_seat(*Test::waylandSeat()));
    configure(true, QStringLiteral("Off"));
    // As fitsAnotherAspectRatio: 256 x 150 fitted between bars on either side.
    std::unique_ptr<KWayland::Client::Surface> surface = Test::createSurface();
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(384, 216);
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    QVERIFY(Test::renderAndWaitForShown(surface.get(), pattern(QSize(256, 150))));
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    const auto heard = [this]() {
        Test::waylandSync();
        m_queue->dispatch();
        return true;
    };
    auto *application = static_cast<WaylandTestApplication *>(kwinApp());
    auto *tablet = application->virtualTablet();
    auto *tool = application->virtualTabletTool();
    const QPointF bar(2, 108);
    const QPointF middle(192, 108);
    quint32 time = 0;
    Test::tabletToolProximityEvent(bar, 0, 0, 0, 0, 0, false, true, ++time);
    QTRY_VERIFY(heard() && !seat.pens.empty());
    TestPen *pen = seat.pens.front().get();
    QVERIFY(heard() && !pen->surface);
    Q_EMIT tablet->tabletToolAxisEvent(middle, 0, 0, 0, 0, 0, false, true, tool, std::chrono::milliseconds(++time), tablet);
    QTRY_VERIFY(heard() && pen->surface == static_cast<wl_surface *>(*surface));
    QVERIFY2(std::abs(pen->position.x() - 192) < 2 && std::abs(pen->position.y() - 108) < 2,
             qPrintable(QStringLiteral("%1, %2").arg(pen->position.x()).arg(pen->position.y())));
    Q_EMIT tablet->tabletToolTipEvent(middle, 1, 0, 0, 0, 0, true, true, tool, std::chrono::milliseconds(++time), tablet);
    QTRY_VERIFY(heard() && pen->down);
    Q_EMIT tablet->tabletToolTipEvent(bar, 0, 0, 0, 0, 0, false, true, tool, std::chrono::milliseconds(++time), tablet);
    QTRY_VERIFY(heard() && !pen->down);
    Test::tabletToolProximityEvent(bar, 0, 0, 0, 0, 0, false, false, ++time);
    QTRY_VERIFY(heard() && !pen->surface);
}
